#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <SciLexer.h>
#include <Scintilla.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

namespace {

using ListLoadWFn = HWND(__stdcall*)(HWND, wchar_t*, int);
using ListLoadNextWFn = int(__stdcall*)(HWND, HWND, wchar_t*, int);
using ListCloseWindowFn = void(__stdcall*)(HWND);
using ListGetDetectStringFn = void(__stdcall*)(char*, int);

HMODULE g_plugin = nullptr;
HWND g_pluginWindow = nullptr;
ListCloseWindowFn g_closePlugin = nullptr;

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (g_pluginWindow) MoveWindow(g_pluginWindow, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_CLOSE:
        if (g_pluginWindow && g_closePlugin) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

bool is_markdown_path(const std::filesystem::path& path) {
    const auto extension = path.extension().wstring();
    return _wcsicmp(extension.c_str(), L".md") == 0 || _wcsicmp(extension.c_str(), L".markdown") == 0 ||
        _wcsicmp(extension.c_str(), L".mkd") == 0 || _wcsicmp(extension.c_str(), L".mkdn") == 0;
}

bool is_html_path(const std::filesystem::path& path) {
    const auto extension = path.extension().wstring();
    return _wcsicmp(extension.c_str(), L".html") == 0 || _wcsicmp(extension.c_str(), L".htm") == 0 ||
        _wcsicmp(extension.c_str(), L".xhtml") == 0 || _wcsicmp(extension.c_str(), L".shtml") == 0;
}

bool is_preview_path(const std::filesystem::path& path) {
    return is_markdown_path(path) || is_html_path(path);
}

bool is_lisp_path(const std::filesystem::path& path) {
    return _wcsicmp(path.extension().c_str(), L".lsp") == 0 ||
        _wcsicmp(path.extension().c_str(), L".lisp") == 0;
}

struct FindControlContext {
    int id;
    HWND result = nullptr;
};

BOOL CALLBACK find_control_by_id_proc(HWND window, LPARAM value) {
    auto* context = reinterpret_cast<FindControlContext*>(value);
    if (GetDlgCtrlID(window) == context->id) {
        context->result = window;
        return FALSE;
    }
    return TRUE;
}

HWND find_control_by_id(HWND parent, int id) {
    FindControlContext context{id};
    EnumChildWindows(parent, find_control_by_id_proc, reinterpret_cast<LPARAM>(&context));
    return context.result;
}

struct FindWindowClassContext {
    const wchar_t* className;
    HWND result = nullptr;
};

BOOL CALLBACK find_thread_window_by_class_proc(HWND window, LPARAM value) {
    auto* context = reinterpret_cast<FindWindowClassContext*>(value);
    wchar_t className[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (_wcsicmp(className, context->className) == 0) {
        context->result = window;
        return FALSE;
    }
    return TRUE;
}

HWND find_thread_window_by_class(const wchar_t* className) {
    FindWindowClassContext context{className};
    EnumThreadWindows(GetCurrentThreadId(), find_thread_window_by_class_proc,
        reinterpret_cast<LPARAM>(&context));
    return context.result;
}

void pump_messages(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    while (GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(
            deadline - GetTickCount64(), 20));
        MsgWaitForMultipleObjects(0, nullptr, FALSE, remaining, QS_ALLINPUT);
    }
}

bool write_recovery_snapshot_for_test(HWND pluginWindow, const std::filesystem::path& dataDirectory) {
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    constexpr char marker[] = "\nEditMdView automatic recovery marker";
    SendMessageA(editor, SCI_APPENDTEXT, sizeof(marker) - 1, reinterpret_cast<LPARAM>(marker));
    if (SendMessageW(editor, SCI_GETMODIFY, 0, 0) == 0) return false;
    SendMessageW(pluginWindow, WM_TIMER, 4, 0);

    const auto recoveryDirectory = dataDirectory / L"recovery";
    bool snapshotFound = false;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(recoveryDirectory, error), end;
            !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        std::ifstream input(iterator->path(), std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (content.find(marker) != std::string::npos) snapshotFound = true;
    }
    return snapshotFound;
}

BOOL CALLBACK accept_recovery_dialog_proc(HWND window, LPARAM value) {
    wchar_t className[32]{};
    wchar_t title[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    if (wcscmp(className, L"#32770") != 0 || wcscmp(title, L"恢复未保存内容") != 0) return TRUE;
    const HWND yes = GetDlgItem(window, IDYES);
    if (!yes) return TRUE;
    PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDYES, BN_CLICKED), reinterpret_cast<LPARAM>(yes));
    *reinterpret_cast<std::atomic_bool*>(value) = true;
    return FALSE;
}

bool default_mode_is_correct(HWND pluginWindow, const std::filesystem::path& path) {
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    const HWND editButton = find_control_by_id(pluginWindow, 1001);
    const HFONT uiFont = editButton
        ? reinterpret_cast<HFONT>(SendMessageW(editButton, WM_GETFONT, 0, 0)) : nullptr;
    LOGFONTW uiFontDescription{};
    if (!uiFont || GetObjectW(uiFont, sizeof(uiFontDescription), &uiFontDescription) == 0 ||
        _wcsicmp(uiFontDescription.lfFaceName, L"MS Shell Dlg") != 0) return false;
    const bool editorVisible = (GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0;
    if (is_preview_path(path) ? editorVisible : !editorVisible) return false;

    const char* expectedLexer = nullptr;
    if (is_markdown_path(path)) expectedLexer = "markdown";
    else if (is_html_path(path)) expectedLexer = "hypertext";
    else if (_wcsicmp(path.filename().c_str(), L"CMakeLists.txt") == 0 ||
        _wcsicmp(path.extension().c_str(), L".cmake") == 0) expectedLexer = "cmake";
    else if (_wcsicmp(path.extension().c_str(), L".cpp") == 0 ||
        _wcsicmp(path.extension().c_str(), L".h") == 0) expectedLexer = "cpp";
    else if (_wcsicmp(path.extension().c_str(), L".lisp") == 0 ||
        _wcsicmp(path.extension().c_str(), L".lsp") == 0) expectedLexer = "lisp";
    else if (_wcsicmp(path.extension().c_str(), L".foo") == 0) expectedLexer = "python";
    if (expectedLexer) {
        char lexerName[64]{};
        SendMessageA(editor, 4012, 0, reinterpret_cast<LPARAM>(lexerName)); // SCI_GETLEXERLANGUAGE
        if (_stricmp(lexerName, expectedLexer) != 0) return false;
    }
    if (_wcsicmp(path.extension().c_str(), L".foo") == 0) {
        constexpr UINT getTabWidth = 2121;
        constexpr UINT getIndent = 2123;
        constexpr UINT getUseTabs = 2125;
        constexpr UINT getWrapMode = 2269;
        constexpr UINT styleGetFore = 2481;
        constexpr UINT styleGetSize = 2485;
        constexpr UINT getMarginWidth = 2243;
        constexpr UINT getIndentationGuides = 2133;
        constexpr WPARAM styleDefault = 32;
        if (SendMessageW(editor, getTabWidth, 0, 0) != 3 ||
            SendMessageW(editor, getIndent, 0, 0) != 3 ||
            SendMessageW(editor, getUseTabs, 0, 0) != 0 ||
            SendMessageW(editor, getWrapMode, 0, 0) != 0 ||
            SendMessageW(editor, styleGetFore, styleDefault, 0) != RGB(0x12, 0x34, 0x56) ||
            SendMessageW(editor, styleGetSize, styleDefault, 0) != 13 ||
            SendMessageW(editor, getMarginWidth, 2, 0) <= 0 ||
            SendMessageW(editor, getIndentationGuides, 0, 0) == 0) return false;
    }
    char expectedFont[256]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_FONT", expectedFont,
            static_cast<DWORD>(std::size(expectedFont))) > 0) {
        char actualFont[256]{};
        SendMessageA(editor, 2486, 32, reinterpret_cast<LPARAM>(actualFont)); // SCI_STYLEGETFONT
        if (strcmp(actualFont, expectedFont) != 0) return false;
    }
    char expectedSize[32]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_SIZE", expectedSize,
            static_cast<DWORD>(std::size(expectedSize))) > 0) {
        const int configuredSize = std::atoi(expectedSize);
        if (SendMessageW(editor, 2485, 32, 0) != configuredSize) return false; // SCI_STYLEGETSIZE
    }
    char fullConfig[8]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_FULL_CONFIG", fullConfig,
            static_cast<DWORD>(std::size(fullConfig))) > 0) {
        char locale[64]{};
        SendMessageA(editor, 2761, 0, reinterpret_cast<LPARAM>(locale)); // SCI_GETFONTLOCALE
        constexpr WPARAM styleDefault = 32;
        constexpr WPARAM commentStyle = 1;
        if (SendMessageW(editor, 2631, 0, 0) != 1 || // SCI_GETTECHNOLOGY
            SendMessageW(editor, 2612, 0, 0) != 3 || // SCI_GETFONTQUALITY
            strcmp(locale, "zh-Hans") != 0 ||
            SendMessageW(editor, 2034, 0, 0) != 1 || // SCI_GETBUFFEREDDRAW
            SendMessageW(editor, 2121, 0, 0) != 8 || // SCI_GETTABWIDTH
            SendMessageW(editor, 2123, 0, 0) != 4 || // SCI_GETINDENT
            SendMessageW(editor, 2125, 0, 0) != 0 || // SCI_GETUSETABS
            SendMessageW(editor, 2261, 0, 0) != 1 || // SCI_GETTABINDENTS
            SendMessageW(editor, 2263, 0, 0) != 1 || // SCI_GETBACKSPACEUNINDENTS
            SendMessageW(editor, 2269, 0, 0) != 1 || // SCI_GETWRAPMODE
            SendMessageW(editor, 2020, 0, 0) != 1 || // SCI_GETVIEWWS
            SendMessageW(editor, 2087, 0, 0) != 2 || // SCI_GETWHITESPACESIZE
            SendMessageW(editor, 2133, 0, 0) != 3 || // SCI_GETINDENTATIONGUIDES
            SendMessageW(editor, 2156, 0, 0) != 10 || // SCI_GETMARGINLEFT
            SendMessageW(editor, 2158, 0, 0) != 10 || // SCI_GETMARGINRIGHT
            SendMessageW(editor, 2243, 1, 0) != 16 || // SCI_GETMARGINWIDTHN
            SendMessageW(editor, 2243, 2, 0) != 20 ||
            SendMessageW(editor, 2762, 0, 0) != 1 || // SCI_GETSELECTIONLAYER
            SendMessageW(editor, 2764, 0, 0) != 1 || // SCI_GETCARETLINELAYER
            SendMessageW(editor, 2704, 0, 0) != 2 || // SCI_GETCARETLINEFRAME
            SendMessageW(editor, 2075, 0, 0) != 500 || // SCI_GETCARETPERIOD
            SendMessageW(editor, 2189, 0, 0) != 2 || // SCI_GETCARETWIDTH
            SendMessageW(editor, 2360, 0, 0) != 80 || // SCI_GETEDGECOLUMN
            SendMessageW(editor, 2362, 0, 0) != 1 || // SCI_GETEDGEMODE
            SendMessageW(editor, 2364, 0, 0) != RGB(0, 255, 0) || // SCI_GETEDGECOLOUR
            SendMessageW(editor, 2374, 0, 0) != 1 || // SCI_GETZOOM
            SendMessageW(editor, 2485, styleDefault, 0) != 13 || // SCI_STYLEGETSIZE
            SendMessageW(editor, 2485, commentStyle, 0) != 12) return false;
    }
    char sciteVisuals[8]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_SCITE_VISUALS", sciteVisuals,
            static_cast<DWORD>(std::size(sciteVisuals))) > 0) {
        const unsigned caretLineElement = static_cast<unsigned>(
            SendMessageW(editor, SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0));
        const unsigned expectedCaretLineElement = static_cast<unsigned>(RGB(0, 0xA2, 0xE9)) | 0xFF000000u;
        if (SendMessageW(editor, SCI_GETCARETFORE, 0, 0) != RGB(0xFF, 0, 0) ||
            SendMessageW(editor, SCI_GETCARETLINEBACK, 0, 0) != RGB(0, 0xA2, 0xE9) ||
            caretLineElement != expectedCaretLineElement ||
            SendMessageW(editor, SCI_GETCARETLINEFRAME, 0, 0) != 2 ||
            SendMessageW(editor, SCI_STYLEGETSIZE, STYLE_DEFAULT, 0) != 13 ||
            SendMessageW(editor, SCI_STYLEGETSIZE, SCE_MARKDOWN_HEADER1, 0) != 13) return false;
    }
    char lispPalette[8]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_LISP_PALETTE", lispPalette,
            static_cast<DWORD>(std::size(lispPalette))) > 0) {
        if (SendMessageW(editor, SCI_STYLEGETFORE, SCE_LISP_NUMBER, 0) != RGB(0, 0, 0) ||
            SendMessageW(editor, SCI_STYLEGETFORE, SCE_LISP_KEYWORD, 0) != RGB(0xFF, 0, 0) ||
            SendMessageW(editor, SCI_STYLEGETFORE, SCE_LISP_STRING, 0) != RGB(0, 0x80, 0) ||
            SendMessageW(editor, SCI_STYLEGETFORE, SCE_LISP_OPERATOR, 0) != RGB(0, 0, 0xFF)) return false;
    }
    return true;
}

bool list_load_next_keeps_position(HWND host, HWND pluginWindow, ListLoadNextWFn loadNext,
    const std::filesystem::path& originalPath) {
    wchar_t nextBuffer[32768]{};
    if (GetEnvironmentVariableW(L"EDITMDVIEW_STATE_SECOND", nextBuffer,
            static_cast<DWORD>(std::size(nextBuffer))) == 0) return true;
    if (!loadNext) return false;
    const std::filesystem::path nextPath(nextBuffer);
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;

    const auto document_line = [editor](int preferred) {
        const int count = std::max(1, static_cast<int>(SendMessageW(editor, SCI_GETLINECOUNT, 0, 0)));
        return std::min(preferred, count - 1);
    };
    const bool originalEditorVisible = (GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0;
    if (!originalEditorVisible) pump_messages(800);

    const int originalCaretLine = document_line(140);
    const LRESULT originalLineStart = SendMessageW(editor, SCI_POSITIONFROMLINE, originalCaretLine, 0);
    const LRESULT originalLineEnd = SendMessageW(editor, SCI_GETLINEENDPOSITION, originalCaretLine, 0);
    const LRESULT originalCaret = std::min(originalLineStart + 5, originalLineEnd);
    const LRESULT originalAnchor = std::min(originalCaret + 7, originalLineEnd);
    SendMessageW(editor, SCI_SETSEL, originalAnchor, originalCaret);
    SendMessageW(editor, SCI_SETFIRSTVISIBLELINE,
        SendMessageW(editor, SCI_VISIBLEFROMDOCLINE, document_line(115), 0), 0);
    const LRESULT originalFirstVisible = SendMessageW(editor, SCI_GETFIRSTVISIBLELINE, 0, 0);

    std::wstring mutableNext = nextPath.wstring();
    if (loadNext(host, pluginWindow, mutableNext.data(), 0) != 0 || !IsWindow(pluginWindow)) return false;
    const int nextCaretLine = document_line(220);
    const LRESULT nextCaret = SendMessageW(editor, SCI_POSITIONFROMLINE, nextCaretLine, 0) + 3;
    SendMessageW(editor, SCI_SETSEL, nextCaret, nextCaret);
    SendMessageW(editor, SCI_SETFIRSTVISIBLELINE,
        SendMessageW(editor, SCI_VISIBLEFROMDOCLINE, 190, 0), 0);

    std::wstring mutableOriginal = originalPath.wstring();
    if (loadNext(host, pluginWindow, mutableOriginal.data(), 0) != 0 || !IsWindow(pluginWindow)) return false;
    if (!originalEditorVisible) pump_messages(100);
    return SendMessageW(editor, SCI_GETANCHOR, 0, 0) == originalAnchor &&
        SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == originalCaret &&
        SendMessageW(editor, SCI_GETFIRSTVISIBLELINE, 0, 0) == originalFirstVisible &&
        ((GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0) == originalEditorVisible;
}

bool editor_commands_work(HWND pluginWindow, const std::filesystem::path& path) {
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    if (is_preview_path(path)) {
        const HWND editButton = find_control_by_id(pluginWindow, 1001);
        const HWND splitButton = find_control_by_id(pluginWindow, 1002);
        const HWND previewButton = find_control_by_id(pluginWindow, 1003);
        if (!editButton || !splitButton || !previewButton) return false;
        const HWND divider = GetDlgItem(pluginWindow, 1010);
        if (!divider) return false;
        SendMessageW(previewButton, BM_CLICK, 0, 0);
        SendMessageW(pluginWindow, WM_APP + 2, 0, 0); // Preview -> Edit.
        const bool shortcutEditVisible = (GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0 &&
            (GetWindowLongPtrW(divider, GWL_STYLE) & WS_VISIBLE) == 0;
        SendMessageW(pluginWindow, WM_APP + 2, 0, 0); // Edit -> Split.
        const bool splitVisible = (GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0;
        const bool dividerVisible =
            (GetWindowLongPtrW(divider, GWL_STYLE) & WS_VISIBLE) != 0;
        bool dividerWorks = dividerVisible;
        int dividerGrowth = 0;
        int dividerResetError = -1;
        if (dividerWorks) {
            RECT pluginBounds{};
            RECT dividerBounds{};
            RECT editorBefore{};
            RECT editorAfter{};
            GetWindowRect(pluginWindow, &pluginBounds);
            GetWindowRect(divider, &dividerBounds);
            GetWindowRect(editor, &editorBefore);
            const int targetX = pluginBounds.left + (pluginBounds.right - pluginBounds.left) * 2 / 3;
            const int targetY = pluginBounds.top + (pluginBounds.bottom - pluginBounds.top) / 2;
            const LPARAM target = MAKELPARAM(targetX - dividerBounds.left, targetY - dividerBounds.top);
            SendMessageW(divider, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            SendMessageW(divider, WM_MOUSEMOVE, MK_LBUTTON, target);
            SendMessageW(divider, WM_LBUTTONUP, 0, target);
            GetWindowRect(editor, &editorAfter);
            dividerGrowth = (editorAfter.right - editorAfter.left) -
                (editorBefore.right - editorBefore.left);
            dividerWorks = dividerGrowth > 40;
            SendMessageW(divider, WM_LBUTTONDBLCLK, MK_LBUTTON, 0);
            RECT editorReset{};
            GetWindowRect(editor, &editorReset);
            const int pluginWidth = pluginBounds.right - pluginBounds.left;
            dividerResetError = std::abs((editorReset.right - editorReset.left) * 2 - pluginWidth);
            dividerWorks = dividerWorks && dividerResetError <= 12;
        }
        SendMessageW(pluginWindow, WM_TIMER, 2, 0); // Flush split-preview scroll synchronization.
        SendMessageW(pluginWindow, WM_APP + 10, 1, 3); // Rendered block click mapped to source line 3.
        const LRESULT reverseMappedLine = SendMessageW(editor, SCI_LINEFROMPOSITION,
            SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0), 0);
        SendMessageW(pluginWindow, WM_APP + 2, 0, 0); // Split -> Preview.
        const bool shortcutPreviewVisible = (GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) == 0 &&
            (GetWindowLongPtrW(divider, GWL_STYLE) & WS_VISIBLE) == 0;
        const bool success = shortcutEditVisible && splitVisible && shortcutPreviewVisible && dividerWorks &&
            reverseMappedLine == 2;
        if (!success) {
            std::fprintf(stderr,
                "preview command check: edit=%d split=%d preview=%d divider=%d handle=%p visible=%d growth=%d reset=%d line=%lld\n",
                shortcutEditVisible, splitVisible, shortcutPreviewVisible, dividerWorks,
                static_cast<void*>(divider), dividerVisible,
                dividerGrowth, dividerResetError, static_cast<long long>(reverseMappedLine));
        }
        return success;
    }

    constexpr UINT getWrapMode = 2269;
    constexpr UINT getZoom = 2374;
    constexpr UINT toggleWrap = WM_APP + 8;
    constexpr UINT changeZoom = WM_APP + 7;
    constexpr UINT setEol = WM_APP + 11;
    constexpr UINT setLanguage = WM_APP + 12;
    const LRESULT wrapBefore = SendMessageW(editor, getWrapMode, 0, 0);
    SendMessageW(pluginWindow, toggleWrap, 0, 0);
    const LRESULT wrapAfter = SendMessageW(editor, getWrapMode, 0, 0);
    SendMessageW(pluginWindow, toggleWrap, 0, 0);
    const LRESULT zoomBefore = SendMessageW(editor, getZoom, 0, 0);
    SendMessageW(pluginWindow, changeZoom, 1, 0);
    const LRESULT zoomAfter = SendMessageW(editor, getZoom, 0, 0);
    SendMessageW(pluginWindow, changeZoom, 0, 0);

    const HWND status = FindWindowExW(pluginWindow, nullptr, L"msctls_statusbar32", nullptr);
    if (!status || SendMessageW(status, SB_GETPARTS, 0, 0) != 5) return false;
    wchar_t languagePanel[128]{};
    wchar_t eolPanel[128]{};
    SendMessageW(status, SB_GETTEXTW, 1, reinterpret_cast<LPARAM>(languagePanel));
    SendMessageW(status, SB_GETTEXTW, 3, reinterpret_cast<LPARAM>(eolPanel));
    if (wcsstr(languagePanel, L"语言:") == nullptr || wcsstr(eolPanel, L"换行符:") == nullptr) return false;

    const LRESULT originalLength = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string originalText(static_cast<std::size_t>(originalLength) + 1, '\0');
    SendMessageA(editor, SCI_GETTEXT, originalLength + 1, reinterpret_cast<LPARAM>(originalText.data()));
    originalText.resize(static_cast<std::size_t>(originalLength));
    std::string viewTestText;
    for (int line = 0; line < 140; ++line) {
        viewTestText += std::to_string(line) + ": " + std::string(420, 'x') + "\n";
    }
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(viewTestText.c_str()));
    if (SendMessageW(editor, getWrapMode, 0, 0) != SC_WRAP_NONE) {
        SendMessageW(pluginWindow, toggleWrap, 0, 0);
    }
    const LRESULT anchoredCaret = SendMessageW(editor, SCI_POSITIONFROMLINE, 82, 0) + 24;
    SendMessageW(editor, SCI_SETSEL, anchoredCaret - 4, anchoredCaret);
    SendMessageW(editor, SCI_SETFIRSTVISIBLELINE, 72, 0);
    const int caretYBefore = static_cast<int>(SendMessageW(
        editor, SCI_POINTYFROMPOSITION, 0, anchoredCaret));
    const int lineHeight = std::max(1, static_cast<int>(SendMessageW(editor, SCI_TEXTHEIGHT, 82, 0)));
    RECT stateBounds{};
    const bool hasStateBounds = SendMessageW(
        status, SB_GETRECT, 4, reinterpret_cast<LPARAM>(&stateBounds)) != 0;
    const LPARAM wrapPoint = MAKELPARAM(stateBounds.left + 6,
        stateBounds.top + (stateBounds.bottom - stateBounds.top) / 2);
    if (hasStateBounds) SendMessageW(status, WM_LBUTTONUP, 0, wrapPoint);
    const int caretYWrapped = static_cast<int>(SendMessageW(
        editor, SCI_POINTYFROMPOSITION, 0, anchoredCaret));
    const bool wrappedByStatus = SendMessageW(editor, getWrapMode, 0, 0) != SC_WRAP_NONE;
    const bool wrappedViewStable = SendMessageW(editor, SCI_GETANCHOR, 0, 0) == anchoredCaret - 4 &&
        SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == anchoredCaret &&
        std::abs(caretYWrapped - caretYBefore) <= lineHeight;
    if (hasStateBounds) SendMessageW(status, WM_LBUTTONUP, 0, wrapPoint);
    const int caretYUnwrapped = static_cast<int>(SendMessageW(
        editor, SCI_POINTYFROMPOSITION, 0, anchoredCaret));
    const bool unwrappedByStatus = SendMessageW(editor, getWrapMode, 0, 0) == SC_WRAP_NONE;
    const bool unwrappedViewStable = SendMessageW(editor, SCI_GETANCHOR, 0, 0) == anchoredCaret - 4 &&
        SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == anchoredCaret &&
        std::abs(caretYUnwrapped - caretYBefore) <= lineHeight;
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(originalText.c_str()));
    SendMessageW(editor, SCI_SETSAVEPOINT, 0, 0);
    if (wrapBefore != SC_WRAP_NONE) SendMessageW(pluginWindow, toggleWrap, 0, 0);
    const bool statusWrapWorks = hasStateBounds && wrappedByStatus && unwrappedByStatus &&
        wrappedViewStable && unwrappedViewStable;

    char originalLexer[64]{};
    SendMessageA(editor, SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(originalLexer));
    WPARAM originalLanguage = 0;
    if (_stricmp(originalLexer, "cmake") == 0) originalLanguage = 14;
    else if (_stricmp(originalLexer, "cpp") == 0) originalLanguage = 2;
    else if (_stricmp(originalLexer, "lisp") == 0) originalLanguage = 20;
    else if (_stricmp(originalLexer, "python") == 0) originalLanguage = 4;
    const LRESULT originalEol = SendMessageW(editor, SCI_GETEOLMODE, 0, 0);
    const LRESULT alternateEol = originalEol == SC_EOL_LF ? SC_EOL_CRLF : SC_EOL_LF;
    SendMessageW(pluginWindow, setEol, static_cast<WPARAM>(alternateEol), 0);
    const bool eolChanged = SendMessageW(editor, SCI_GETEOLMODE, 0, 0) == alternateEol &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) != 0;
    SendMessageW(editor, SCI_UNDO, 0, 0);
    SendMessageW(pluginWindow, setEol, originalEol, 0);

    SendMessageW(pluginWindow, setLanguage, 1, 0); // Markdown
    char switchedLexer[64]{};
    SendMessageA(editor, SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(switchedLexer));
    const bool languageChanged = _stricmp(switchedLexer, "markdown") == 0 &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) == 0;
    SendMessageW(pluginWindow, setLanguage, originalLanguage, 0);

    return wrapBefore != wrapAfter && statusWrapWorks && zoomAfter > zoomBefore && eolChanged && languageChanged &&
        SendMessageW(editor, SCI_GETEOLMODE, 0, 0) == originalEol;
}

bool editor_text_input_works(HWND pluginWindow, const std::filesystem::path& path) {
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;

    const bool previewDocument = is_preview_path(path);
    if (previewDocument) {
        const HWND editButton = find_control_by_id(pluginWindow, 1001);
        if (!editButton) return false;
        SendMessageW(editButton, BM_CLICK, 0, 0);
    }
    if ((GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) == 0 ||
        SendMessageW(editor, SCI_GETREADONLY, 0, 0) != 0) return false;
    if (SendMessageW(editor, SCI_GETSELECTIONHIDDEN, 0, 0) != 0 ||
        SendMessageW(editor, SCI_GETCARETSTYLE, 0, 0) == CARETSTYLE_INVISIBLE ||
        SendMessageW(editor, SCI_GETCARETWIDTH, 0, 0) <= 0) return false;

    const LRESULT lengthBefore = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    const LRESULT modifiedBefore = SendMessageW(editor, SCI_GETMODIFY, 0, 0);
    BYTE originalKeyboard[256]{};
    if (GetKeyboardState(originalKeyboard)) {
        BYTE controlKeyboard[256]{};
        memcpy(controlKeyboard, originalKeyboard, sizeof(controlKeyboard));
        controlKeyboard[VK_CONTROL] = 0x80;
        SetKeyboardState(controlKeyboard);
        const std::pair<WPARAM, WPARAM> controlKeys[] = {
            {'B', 0x02}, {'F', 0x06}, {'H', 0x08}};
        for (const auto [key, character] : controlKeys) {
            if (key == 'B' && is_markdown_path(path)) continue;
            SendMessageW(editor, WM_KEYDOWN, key, 1);
            SendMessageW(editor, WM_CHAR, character, 1);
            SendMessageW(editor, WM_KEYUP, key, 1);
        }
        SetKeyboardState(originalKeyboard);
        if (SendMessageW(editor, SCI_GETLENGTH, 0, 0) != lengthBefore) return false;
    }
    SendMessageW(editor, SCI_GOTOPOS, lengthBefore, 0);
    SendMessageW(editor, SCI_BEGINUNDOACTION, 0, 0);
    SendMessageW(editor, WM_CHAR, L'g', 1);
    SendMessageW(editor, SCI_ENDUNDOACTION, 0, 0);

    const bool inserted = SendMessageW(editor, SCI_GETLENGTH, 0, 0) == lengthBefore + 1;
    // The bundled configuration retains the user's original value 2. SciTE
    // only enables automatic document-word completion for the exact value 1.
    const bool completionStayedClosed = SendMessageW(editor, SCI_AUTOCACTIVE, 0, 0) == 0;
    if (!completionStayedClosed) SendMessageW(editor, SCI_AUTOCCANCEL, 0, 0);
    SendMessageW(editor, SCI_UNDO, 0, 0);
    const bool restored = SendMessageW(editor, SCI_GETLENGTH, 0, 0) == lengthBefore &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) == modifiedBefore;

    if (previewDocument) {
        const HWND previewButton = find_control_by_id(pluginWindow, 1003);
        if (!previewButton) return false;
        SendMessageW(previewButton, BM_CLICK, 0, 0);
        if ((GetWindowLongPtrW(editor, GWL_STYLE) & WS_VISIBLE) != 0) return false;
    }
    return inserted && completionStayedClosed && restored;
}

bool save_cleanup_keeps_selection(HWND pluginWindow, const std::filesystem::path& path) {
    char enabled[8]{};
    if (GetEnvironmentVariableA("EDITMDVIEW_EXPECT_SAVE_CARET", enabled,
            static_cast<DWORD>(std::size(enabled))) == 0) return true;

    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    constexpr char original[] = "alpha   \r\n\t  \nbeta  \rgamma";
    constexpr std::string_view expected = "alpha\n\nbeta\ngamma\n";
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(original));
    SendMessageW(editor, SCI_SETEOLMODE, SC_EOL_LF, 0);
    SendMessageW(editor, SCI_SETSEL, 24, 16); // Reversed selection spanning cleaned lines.
    SendMessageW(pluginWindow, WM_APP + 1, 0, 0); // Save.

    const LRESULT length = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string actual(static_cast<std::size_t>(length + 1), '\0');
    SendMessageA(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(actual.data()));
    actual.resize(static_cast<std::size_t>(length));
    std::ifstream input(path, std::ios::binary);
    const std::string saved((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    input.close();
    const bool selectionPreserved = actual == expected && saved == expected &&
        SendMessageW(editor, SCI_GETANCHOR, 0, 0) == 15 &&
        SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == 9 &&
        SendMessageW(editor, SCI_GETSELECTIONSTART, 0, 0) == 9 &&
        SendMessageW(editor, SCI_GETSELECTIONEND, 0, 0) == 15 &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) == 0;

    // Trailing spaces can create many wrapped display lines before saving.
    // Restoring the old absolute display-line number would then scroll the
    // cleaned caret above the viewport even though its document line is stable.
    std::string wrappedOriginal;
    std::string wrappedExpected;
    for (int line = 0; line < 70; ++line) {
        const std::string prefix = "line-" + std::to_string(line);
        wrappedOriginal += prefix + std::string(320, ' ') + "\n";
        wrappedExpected += prefix + "\n";
    }
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(wrappedOriginal.c_str()));
    SendMessageW(editor, SCI_SETEOLMODE, SC_EOL_LF, 0);
    const LRESULT caretPosition = SendMessageW(editor, SCI_POSITIONFROMLINE, 50, 0) + 3;
    SendMessageW(editor, SCI_SETSEL, caretPosition, caretPosition);
    const LRESULT intendedTop = SendMessageW(editor, SCI_VISIBLEFROMDOCLINE, 45, 0);
    SendMessageW(editor, SCI_SETFIRSTVISIBLELINE, intendedTop, 0);
    const LRESULT actualTopDocumentLine = SendMessageW(editor, SCI_DOCLINEFROMVISIBLE,
        SendMessageW(editor, SCI_GETFIRSTVISIBLELINE, 0, 0), 0);
    SendMessageW(pluginWindow, WM_APP + 1, 0, 0); // Save after wrapped whitespace cleanup.

    RECT editorBounds{};
    GetClientRect(editor, &editorBounds);
    const LRESULT cleanedCaret = SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0);
    const LRESULT caretY = SendMessageW(editor, SCI_POINTYFROMPOSITION, 0, cleanedCaret);
    const LRESULT restoredTopDocumentLine = SendMessageW(editor, SCI_DOCLINEFROMVISIBLE,
        SendMessageW(editor, SCI_GETFIRSTVISIBLELINE, 0, 0), 0);
    std::ifstream wrappedInput(path, std::ios::binary);
    const std::string wrappedSaved((std::istreambuf_iterator<char>(wrappedInput)),
        std::istreambuf_iterator<char>());
    return selectionPreserved && wrappedSaved == wrappedExpected &&
        SendMessageW(editor, SCI_LINEFROMPOSITION, cleanedCaret, 0) == 50 &&
        restoredTopDocumentLine == actualTopDocumentLine &&
        caretY >= 0 && caretY < editorBounds.bottom;
}

int markdown_editing_failure(HWND pluginWindow, const std::filesystem::path& path) {
    if (!is_markdown_path(path)) return 0;
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    const HWND editButton = find_control_by_id(pluginWindow, 1001);
    const HWND previewButton = find_control_by_id(pluginWindow, 1003);
    if (!editor || !editButton || !previewButton) return 1;
    SendMessageW(editButton, BM_CLICK, 0, 0);

    auto editor_text = [&]() {
        const LRESULT length = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
        std::string value(static_cast<std::size_t>(length + 1), '\0');
        SendMessageA(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(value.data()));
        value.resize(static_cast<std::size_t>(length));
        return value;
    };
    const std::string original = editor_text();
    int failure = 0;
    auto verify = [&](bool condition, int stage) {
        if (!condition && failure == 0) failure = stage;
    };

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    SendMessageW(editor, SCI_GOTOPOS, 0, 0);
    SendMessageW(editor, WM_CHAR, L'/', 1);
    HWND slashPopup = FindWindowExW(pluginWindow, nullptr, L"EditMdView.MarkdownSlash", nullptr);
    verify(slashPopup != nullptr &&
        (GetWindowLongPtrW(slashPopup, GWL_STYLE) & WS_VISIBLE) != 0, 2);
    SendMessageW(editor, WM_KEYDOWN, VK_ESCAPE, 1);
    verify(FindWindowExW(pluginWindow, nullptr,
        L"EditMdView.MarkdownSlash", nullptr) == nullptr && editor_text() == "/", 3);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    SendMessageW(editor, SCI_GOTOPOS, 0, 0);
    SendMessageW(editor, WM_CHAR, L'/', 1);
    slashPopup = FindWindowExW(pluginWindow, nullptr, L"EditMdView.MarkdownSlash", nullptr);
    if (slashPopup) {
        RECT popupBounds{};
        GetWindowRect(slashPopup, &popupBounds);
        SendMessageW(editor, WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
            MAKELPARAM(popupBounds.left + 20, popupBounds.top + 20));
        SendMessageW(slashPopup, WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
    }
    verify(editor_text() == "## ", 14);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("alpha"));
    SendMessageW(editor, SCI_SETSEL, 0, 5);
    BYTE originalKeyboard[256]{};
    if (GetKeyboardState(originalKeyboard)) {
        BYTE controlKeyboard[256]{};
        memcpy(controlKeyboard, originalKeyboard, sizeof(controlKeyboard));
        controlKeyboard[VK_CONTROL] = 0x80;
        SetKeyboardState(controlKeyboard);
        SendMessageW(editor, WM_KEYDOWN, 'B', 1);
        SendMessageW(editor, WM_CHAR, 0x02, 1);
        SendMessageW(editor, WM_KEYUP, 'B', 1);
        SetKeyboardState(originalKeyboard);
    }
    verify(editor_text() == "**alpha**" &&
        SendMessageW(editor, SCI_GETSELECTIONSTART, 0, 0) == 2 &&
        SendMessageW(editor, SCI_GETSELECTIONEND, 0, 0) == 7, 4);
    SendMessageW(pluginWindow, WM_APP + 13, 0, 0); // Bold again: remove surrounding markers.
    verify(editor_text() == "alpha", 5);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("**alpha**"));
    SendMessageW(editor, SCI_SETSEL, 2, 7);
    SendMessageW(pluginWindow, WM_APP + 13, 1, 0); // Add italic inside bold.
    verify(editor_text() == "***alpha***", 12);
    SendMessageW(pluginWindow, WM_APP + 13, 1, 0); // Remove italic, retain bold.
    verify(editor_text() == "**alpha**", 13);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("word"));
    SendMessageW(editor, SCI_SETSEL, 0, 4);
    SendMessageW(pluginWindow, WM_APP + 13, 2, 0); // Highlight.
    verify(editor_text() == "==word==", 6);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    SendMessageW(editor, SCI_GOTOPOS, 0, 0);
    for (const wchar_t character : std::wstring_view(L"/h1")) {
        SendMessageW(editor, WM_CHAR, character, 1);
    }
    SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 1);
    // Reproduce the WM_CHAR generated by TranslateMessage after WM_KEYDOWN.
    SendMessageW(editor, WM_CHAR, L'\r', 1);
    verify(editor_text() == "# " && SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == 2 &&
        SendMessageW(editor, SCI_GETSELECTIONSTART, 0, 0) == 2 &&
        SendMessageW(editor, SCI_GETSELECTIONEND, 0, 0) == 2, 15);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("/h2"));
    SendMessageW(editor, SCI_GOTOPOS, 3, 0);
    SendMessageW(pluginWindow, WM_APP + 14, 1, 0); // h2
    verify(editor_text() == "## ", 7);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("/note"));
    SendMessageW(editor, SCI_GOTOPOS, 5, 0);
    SendMessageW(pluginWindow, WM_APP + 14, 9, 0); // v1 / note
    verify(editor_text() == "> [!note]\n> ", 8);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    SendMessageW(editor, SCI_GOTOPOS, 0, 0);
    for (const wchar_t character : std::wstring_view(L"/table")) {
        SendMessageW(editor, WM_CHAR, character, 1);
    }
    slashPopup = FindWindowExW(pluginWindow, nullptr, L"EditMdView.MarkdownSlash", nullptr);
    verify(slashPopup != nullptr, 9);
    if (slashPopup) {
        SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 1);
        const HWND rows = GetDlgItem(slashPopup, 5101);
        const HWND columns = GetDlgItem(slashPopup, 5102);
        verify(rows != nullptr && columns != nullptr, 10);
        if (rows) SendMessageW(rows, WM_KEYDOWN, VK_ESCAPE, 1);
    }
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("/table"));
    SendMessageW(editor, SCI_GOTOPOS, 6, 0);
    SendMessageW(pluginWindow, WM_APP + 14, 8, MAKELPARAM(3, 3));
    const std::string table = editor_text();
    verify(table.find("| 标题 1 | 标题 2 | 标题 3 |") == 0 &&
        table.find("| --- | --- | --- |") != std::string::npos, 11);

    const std::string eol = SendMessageW(editor, SCI_GETEOLMODE, 0, 0) == SC_EOL_CRLF
        ? "\r\n" : (SendMessageW(editor, SCI_GETEOLMODE, 0, 0) == SC_EOL_CR ? "\r" : "\n");
    auto smart_enter = [&] {
        SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 1);
        SendMessageW(editor, WM_CHAR, L'\r', 1);
    };
    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("- item"));
    SendMessageW(editor, SCI_GOTOPOS, 6, 0);
    smart_enter();
    verify(editor_text() == "- item" + eol + "- ", 16);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("9. item"));
    SendMessageW(editor, SCI_GOTOPOS, 7, 0);
    smart_enter();
    verify(editor_text() == "9. item" + eol + "10. ", 17);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("- [x] done"));
    SendMessageW(editor, SCI_GOTOPOS, 10, 0);
    smart_enter();
    verify(editor_text() == "- [x] done" + eol + "- [ ] ", 18);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("- "));
    SendMessageW(editor, SCI_GOTOPOS, 2, 0);
    smart_enter();
    verify(editor_text().empty(), 19);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("- item"));
    SendMessageW(editor, SCI_GOTOPOS, 6, 0);
    const int indentWidth = std::max(1, static_cast<int>(SendMessageW(editor, SCI_GETINDENT, 0, 0)));
    const bool useTabs = SendMessageW(editor, SCI_GETUSETABS, 0, 0) != 0;
    SendMessageW(editor, WM_KEYDOWN, VK_TAB, 1);
    SendMessageW(editor, WM_CHAR, L'\t', 1);
    const std::string indentation = useTabs ? "\t" : std::string(static_cast<std::size_t>(indentWidth), ' ');
    verify(editor_text() == indentation + "- item", 20);
    BYTE tabKeyboard[256]{};
    if (GetKeyboardState(tabKeyboard)) {
        BYTE shiftedKeyboard[256]{};
        memcpy(shiftedKeyboard, tabKeyboard, sizeof(tabKeyboard));
        shiftedKeyboard[VK_SHIFT] = 0x80;
        SetKeyboardState(shiftedKeyboard);
        SendMessageW(editor, WM_KEYDOWN, VK_TAB, 1);
        SendMessageW(editor, WM_CHAR, L'\t', 1);
        SendMessageW(editor, WM_KEYUP, VK_TAB, 1);
        SetKeyboardState(tabKeyboard);
    }
    verify(editor_text() == "- item", 21);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("word"));
    SendMessageW(editor, SCI_SETSEL, 0, 4);
    SendMessageW(editor, WM_CHAR, L'`', 1);
    verify(editor_text() == "`word`" &&
        SendMessageW(editor, SCI_GETSELECTIONSTART, 0, 0) == 1 &&
        SendMessageW(editor, SCI_GETSELECTIONEND, 0, 0) == 5, 22);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));
    SendMessageW(editor, SCI_GOTOPOS, 0, 0);
    SendMessageW(editor, WM_CHAR, L'[', 1);
    verify(editor_text() == "[]" && SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == 1, 23);
    SendMessageW(editor, WM_CHAR, L']', 1);
    verify(editor_text() == "[]" && SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0) == 2, 24);

    SendMessageA(editor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(original.c_str()));
    SendMessageW(editor, SCI_SETSAVEPOINT, 0, 0);
    SendMessageW(previewButton, BM_CLICK, 0, 0);
    return failure;
}

bool editor_replace_works(HWND pluginWindow, const std::filesystem::path& path) {
    static bool expectPersistedHistory = false;
    static bool expectDeletedHistoryMissing = false;
    if (!is_lisp_path(path)) return true;
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    const LRESULT length = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string before(static_cast<std::size_t>(length + 1), '\0');
    SendMessageA(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(before.data()));
    before.resize(static_cast<std::size_t>(length));
    const std::size_t position = before.find("greeting");
    if (position == std::string::npos) return false;

    SendMessageW(editor, SCI_SETSEL, position, position + 8);
    const HWND replaceButton = find_control_by_id(pluginWindow, 1009);
    if (!replaceButton) return false;
    SendMessageW(replaceButton, BM_CLICK, 0, 0);
    const HWND replaceWindow = find_thread_window_by_class(L"EditMdView.Replace");
    if (!replaceWindow || !IsWindowEnabled(pluginWindow)) return false;
    const HWND findCombo = GetDlgItem(replaceWindow, 3101);
    const HWND replaceCombo = GetDlgItem(replaceWindow, 3102);
    if (!findCombo || !replaceCombo) return false;
    const bool persistedHistory = !expectPersistedHistory ||
        (SendMessageW(findCombo, CB_GETCOUNT, 0, 0) > 0 &&
            SendMessageW(replaceCombo, CB_GETCOUNT, 0, 0) > 0);
    const bool deletedHistoryMissing = !expectDeletedHistoryMissing ||
        (SendMessageW(findCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
             reinterpret_cast<LPARAM>(L"history-to-delete")) == CB_ERR &&
         SendMessageW(replaceCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
             reinterpret_cast<LPARAM>(L"replacement-to-delete")) == CB_ERR);
    wchar_t comboClass[32]{};
    GetClassNameW(findCombo, comboClass, static_cast<int>(std::size(comboClass)));
    if (_wcsicmp(comboClass, L"ComboBox") != 0) return false;
    wchar_t prefilledFind[64]{};
    GetWindowTextW(findCombo, prefilledFind, static_cast<int>(std::size(prefilledFind)));
    const bool replacePrefilled = wcscmp(prefilledFind, L"greeting") == 0;
    SetWindowTextW(replaceCombo, L"salutation");
    SendMessageW(replaceWindow, WM_COMMAND, 3107, 0);
    const bool historyPopulated = SendMessageW(findCombo, CB_GETCOUNT, 0, 0) > 0 &&
        SendMessageW(replaceCombo, CB_GETCOUNT, 0, 0) > 0;
    expectPersistedHistory = historyPopulated;

    const LRESULT changedLength = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string changed(static_cast<std::size_t>(changedLength + 1), '\0');
    SendMessageA(editor, SCI_GETTEXT, changedLength + 1, reinterpret_cast<LPARAM>(changed.data()));
    const bool replaced = changed.find("salutation") != std::string::npos;

    SetWindowTextW(findCombo, L"history-to-delete");
    SetWindowTextW(replaceCombo, L"replacement-to-delete");
    SendMessageW(replaceWindow, WM_COMMAND, 3108, 0); // Remember both values; no document match.
    auto shift_delete_selected = [](HWND combo, const wchar_t* expected) {
        const LRESULT before = SendMessageW(combo, CB_GETCOUNT, 0, 0);
        if (before <= 1 || SendMessageW(combo, CB_SETCURSEL, 0, 0) == CB_ERR) return false;
        wchar_t selected[128]{};
        SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(selected));
        if (wcscmp(selected, expected) != 0) return false;
        const HWND edit = FindWindowExW(combo, nullptr, L"Edit", nullptr);
        if (!edit) return false;
        BYTE originalKeyboard[256]{};
        if (!GetKeyboardState(originalKeyboard)) return false;
        BYTE shiftedKeyboard[256]{};
        memcpy(shiftedKeyboard, originalKeyboard, sizeof(shiftedKeyboard));
        shiftedKeyboard[VK_SHIFT] = 0x80;
        SetKeyboardState(shiftedKeyboard);
        SendMessageW(edit, WM_KEYDOWN, VK_DELETE, 1);
        SendMessageW(edit, WM_KEYUP, VK_DELETE, 1);
        SetKeyboardState(originalKeyboard);
        return SendMessageW(combo, CB_GETCOUNT, 0, 0) == before - 1 &&
            SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                reinterpret_cast<LPARAM>(expected)) == CB_ERR;
    };
    const bool findHistoryDeleted = shift_delete_selected(findCombo, L"history-to-delete");
    const bool replaceHistoryDeleted = shift_delete_selected(replaceCombo, L"replacement-to-delete");
    expectDeletedHistoryMissing = findHistoryDeleted && replaceHistoryDeleted;

    SendMessageW(editor, SCI_UNDO, 0, 0);
    const LRESULT restoredLength = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string restored(static_cast<std::size_t>(restoredLength + 1), '\0');
    SendMessageA(editor, SCI_GETTEXT, restoredLength + 1, reinterpret_cast<LPARAM>(restored.data()));
    restored.resize(static_cast<std::size_t>(restoredLength));
    SendMessageW(replaceWindow, WM_CLOSE, 0, 0);
    const std::size_t secondSelection = before.find("EditMdView");
    bool findPrefilled = false;
    if (secondSelection != std::string::npos) {
        SendMessageW(editor, SCI_SETSEL, secondSelection, secondSelection + 10);
        SendMessageW(pluginWindow, WM_APP + 3, 0, 0);
        const HWND toolbarFind = find_control_by_id(pluginWindow, 1006);
        wchar_t prefilledToolbarFind[64]{};
        if (toolbarFind) {
            GetWindowTextW(toolbarFind, prefilledToolbarFind,
                static_cast<int>(std::size(prefilledToolbarFind)));
            findPrefilled = wcscmp(prefilledToolbarFind, L"EditMdView") == 0;
        }
    }
    return replaced && replacePrefilled && findPrefilled && historyPopulated &&
        persistedHistory && deletedHistoryMissing && findHistoryDeleted && replaceHistoryDeleted &&
        restored == before &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) == 0;
}

bool external_reload_works(HWND pluginWindow, const std::filesystem::path& path) {
    const HWND editor = FindWindowExW(pluginWindow, nullptr, L"Scintilla", nullptr);
    if (!editor) return false;
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << "# Updated outside EditMdView\n\nExternal reload marker 2026.\n";
        if (!stream) return false;
    }
    SendMessageW(pluginWindow, WM_TIMER, 3, 0);
    pump_messages(100);
    const LRESULT length = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
    std::string text(static_cast<std::size_t>(length + 1), '\0');
    SendMessageA(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()));
    return text.find("External reload marker 2026.") != std::string::npos &&
        SendMessageW(editor, SCI_GETMODIFY, 0, 0) == 0;
}

bool background_preview_finishes(HWND pluginWindow) {
    const HWND status = FindWindowExW(pluginWindow, nullptr, L"msctls_statusbar32", nullptr);
    if (!status) return false;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        wchar_t state[512]{};
        SendMessageW(status, SB_GETTEXTW, 4, reinterpret_cast<LPARAM>(state));
        if (wcsstr(state, L"正在后台生成预览") == nullptr) return true;
        pump_messages(25);
    }
    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const std::filesystem::path pluginPath = argumentCount >= 2
        ? std::filesystem::path(arguments[1])
        : executable_directory() / L"EditMdView.wlx64";
    const std::filesystem::path documentPath = argumentCount >= 3
        ? std::filesystem::path(arguments[2])
        : executable_directory() / L"demo.md";
    const int requestedRepeatCount = argumentCount >= 4 ? _wtoi(arguments[3]) : 1;
    const int repeatCount = requestedRepeatCount > 0 ? requestedRepeatCount : 1;
    std::filesystem::path runtimeDataDirectory;
    const DWORD runtimeDataLength = GetEnvironmentVariableW(L"EDITMDVIEW_DATA_DIR", nullptr, 0);
    if (runtimeDataLength > 0) {
        std::wstring value(static_cast<std::size_t>(runtimeDataLength), L'\0');
        const DWORD written = GetEnvironmentVariableW(
            L"EDITMDVIEW_DATA_DIR", value.data(), runtimeDataLength);
        if (written > 0 && written < runtimeDataLength) {
            value.resize(written);
            runtimeDataDirectory = value;
        }
    }
    if (runtimeDataDirectory.empty()) {
        runtimeDataDirectory = executable_directory() / L"test-runtime-data" /
            std::to_wstring(GetCurrentProcessId());
        std::error_code cleanupError;
        std::filesystem::remove_all(runtimeDataDirectory, cleanupError);
        std::filesystem::create_directories(runtimeDataDirectory, cleanupError);
        SetEnvironmentVariableW(L"EDITMDVIEW_DATA_DIR", runtimeDataDirectory.c_str());
    }
    const bool externalReloadTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_EXPECT_EXTERNAL_RELOAD", nullptr, 0) > 0;
    const bool backgroundPreviewTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_EXPECT_BACKGROUND_PREVIEW", nullptr, 0) > 0;
    const bool persistentStateTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_EXPECT_PERSISTENT_STATE", nullptr, 0) > 0;
    const bool recoverySnapshotTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_EXPECT_RECOVERY_SNAPSHOT", nullptr, 0) > 0;
    const bool configurationReloadTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_EXPECT_CONFIG_RELOAD", nullptr, 0) > 0;
    if (persistentStateTest || recoverySnapshotTest || configurationReloadTest) {
        std::error_code cleanupError;
        std::filesystem::remove_all(runtimeDataDirectory, cleanupError);
        cleanupError.clear();
        std::filesystem::create_directories(runtimeDataDirectory, cleanupError);
    }
    std::filesystem::path reloadConfigurationFile;
    if (configurationReloadTest) {
        const auto configurationDirectory = runtimeDataDirectory / L"reload-config";
        std::error_code directoryError;
        std::filesystem::create_directories(configurationDirectory, directoryError);
        reloadConfigurationFile = configurationDirectory / L"SciTEUser.properties";
        std::ofstream output(reloadConfigurationFile, std::ios::binary | std::ios::trunc);
        output << "tabsize.*=2\nindent.size.*=2\n";
        output.close();
        SetEnvironmentVariableW(L"SciTE_HOME", configurationDirectory.c_str());
        SetEnvironmentVariableW(L"SciTE_USERHOME", configurationDirectory.c_str());
    }
    if (arguments) LocalFree(arguments);

    if (externalReloadTest) {
        std::ofstream stream(documentPath, std::ios::binary | std::ios::trunc);
        stream << "# Initial external reload document\n\nOriginal marker.\n";
        if (!stream) return 17;
    }
    if (backgroundPreviewTest) {
        std::ofstream stream(documentPath, std::ios::binary | std::ios::trunc);
        if (!stream) return 18;
        for (int index = 0; index < 30000; ++index) {
            stream << "## Background section " << index << "\n\n"
                << "Text for asynchronous Markdown rendering and source mapping.\n\n";
        }
        if (!stream) return 18;
    }

    g_plugin = LoadLibraryW(pluginPath.c_str());
    if (!g_plugin) {
        MessageBoxW(nullptr, (L"无法加载插件：\n" + pluginPath.wstring()).c_str(), L"WLX Harness", MB_OK | MB_ICONERROR);
        return 1;
    }
    auto loadPlugin = reinterpret_cast<ListLoadWFn>(GetProcAddress(g_plugin, "ListLoadW"));
    const auto loadNextPlugin = reinterpret_cast<ListLoadNextWFn>(GetProcAddress(g_plugin, "ListLoadNextW"));
    g_closePlugin = reinterpret_cast<ListCloseWindowFn>(GetProcAddress(g_plugin, "ListCloseWindow"));
    const auto getDetectString = reinterpret_cast<ListGetDetectStringFn>(
        GetProcAddress(g_plugin, "ListGetDetectString"));
    if (!loadPlugin || !g_closePlugin || !getDetectString) {
        MessageBoxW(nullptr, L"插件缺少必要的 WLX 导出函数。", L"WLX Harness", MB_OK | MB_ICONERROR);
        FreeLibrary(g_plugin);
        return 2;
    }
    char detectString[2048]{};
    getDetectString(detectString, static_cast<int>(std::size(detectString)));
    if (detectString[0] != '\0') {
        FreeLibrary(g_plugin);
        return 8;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = host_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"EditMdView.WLXHarness";
    RegisterClassExW(&windowClass);

    HWND host = CreateWindowExW(0, windowClass.lpszClassName, L"EditMdView — WLX 预览",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760,
        nullptr, nullptr, instance, nullptr);
    if (!host) {
        FreeLibrary(g_plugin);
        return 3;
    }

    if (repeatCount == 1) {
        ShowWindow(host, showCommand);
        UpdateWindow(host);
        SetWindowTextW(host, L"EditMdView — 正在加载插件…");
    }

    std::wstring mutablePath = documentPath.wstring();
    g_pluginWindow = loadPlugin(host, mutablePath.data(), 0);
    if (!g_pluginWindow) {
        MessageBoxW(host, (L"插件无法打开：\n" + documentPath.wstring()).c_str(), L"WLX Harness", MB_OK | MB_ICONERROR);
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 4;
    }
    if (!default_mode_is_correct(g_pluginWindow, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 6;
    }
    if (configurationReloadTest) {
        const HWND editor = FindWindowExW(g_pluginWindow, nullptr, L"Scintilla", nullptr);
        const bool initial = editor && SendMessageW(editor, SCI_GETTABWIDTH, 0, 0) == 2;
        {
            std::ofstream output(reloadConfigurationFile, std::ios::binary | std::ios::trunc);
            output << "tabsize.*=7\nindent.size.*=7\n";
        }
        SendMessageW(g_pluginWindow, WM_TIMER, 3, 0);
        const bool reloaded = editor && SendMessageW(editor, SCI_GETTABWIDTH, 0, 0) == 7;
        wchar_t state[256]{};
        const HWND status = FindWindowExW(g_pluginWindow, nullptr, STATUSCLASSNAMEW, nullptr);
        if (status) SendMessageW(status, SB_GETTEXTW, 4, reinterpret_cast<LPARAM>(state));
        const bool announced = wcsstr(state, L"配置已自动重新加载") != nullptr;
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return initial && reloaded && announced ? 0 : 42;
    }
    if (recoverySnapshotTest) {
        const bool snapshotWritten = write_recovery_snapshot_for_test(
            g_pluginWindow, runtimeDataDirectory);
        DestroyWindow(g_pluginWindow); // Simulate a host/process loss without the normal close path.
        g_pluginWindow = nullptr;
        std::atomic_bool dialogAccepted = false;
        std::atomic_bool stopDialogThread = false;
        const DWORD windowThread = GetCurrentThreadId();
        std::thread dialogThread([windowThread, &dialogAccepted, &stopDialogThread] {
            while (!stopDialogThread.load()) {
                EnumThreadWindows(windowThread, accept_recovery_dialog_proc,
                    reinterpret_cast<LPARAM>(&dialogAccepted));
                if (dialogAccepted.load()) return;
                Sleep(10);
            }
        });
        mutablePath = documentPath.wstring();
        g_pluginWindow = loadPlugin(host, mutablePath.data(), 0);
        stopDialogThread = true;
        dialogThread.join();
        const HWND recoveredEditor = g_pluginWindow
            ? FindWindowExW(g_pluginWindow, nullptr, L"Scintilla", nullptr) : nullptr;
        bool recovered = false;
        if (recoveredEditor) {
            const LRESULT length = SendMessageW(recoveredEditor, SCI_GETLENGTH, 0, 0);
            std::string text(static_cast<std::size_t>(length) + 1, '\0');
            SendMessageA(recoveredEditor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<std::size_t>(length));
            recovered = snapshotWritten && dialogAccepted.load() &&
                SendMessageW(recoveredEditor, SCI_GETMODIFY, 0, 0) != 0 &&
                text.find("EditMdView automatic recovery marker") != std::string::npos &&
                (GetWindowLongPtrW(recoveredEditor, GWL_STYLE) & WS_VISIBLE) != 0;
            SendMessageW(recoveredEditor, SCI_SETSAVEPOINT, 0, 0);
        }
        if (g_pluginWindow) g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return recovered ? 0 : 40;
    }
    if (persistentStateTest) {
        const HWND editor = FindWindowExW(g_pluginWindow, nullptr, L"Scintilla", nullptr);
        const HWND splitButton = find_control_by_id(g_pluginWindow, 1002);
        if (!editor || !splitButton) return 41;
        SendMessageW(splitButton, BM_CLICK, 0, 0);
        const LRESULT length = SendMessageW(editor, SCI_GETLENGTH, 0, 0);
        const LRESULT expectedCaret = std::min<LRESULT>(length, 37);
        SendMessageW(editor, SCI_SETSEL, expectedCaret, expectedCaret);
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        mutablePath = documentPath.wstring();
        g_pluginWindow = loadPlugin(host, mutablePath.data(), 0);
        pump_messages(100);
        const HWND reopenedEditor = g_pluginWindow
            ? FindWindowExW(g_pluginWindow, nullptr, L"Scintilla", nullptr) : nullptr;
        const HWND divider = g_pluginWindow ? find_control_by_id(g_pluginWindow, 1010) : nullptr;
        const bool restored = reopenedEditor && divider &&
            (GetWindowLongPtrW(reopenedEditor, GWL_STYLE) & WS_VISIBLE) != 0 &&
            (GetWindowLongPtrW(divider, GWL_STYLE) & WS_VISIBLE) != 0 &&
            SendMessageW(reopenedEditor, SCI_GETCURRENTPOS, 0, 0) == expectedCaret;
        if (g_pluginWindow) g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return restored ? 0 : 41;
    }
    if (externalReloadTest) {
        const bool reloaded = external_reload_works(g_pluginWindow, documentPath);
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return reloaded ? 0 : 17;
    }
    if (backgroundPreviewTest) {
        const bool completed = background_preview_finishes(g_pluginWindow);
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return completed ? 0 : 18;
    }
    const bool stateSwitchTest = GetEnvironmentVariableW(
        L"EDITMDVIEW_STATE_SECOND", nullptr, 0) > 0;
    if (!list_load_next_keeps_position(host, g_pluginWindow, loadNextPlugin, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 16;
    }
    if (stateSwitchTest) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        g_plugin = nullptr;
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }
    if (!save_cleanup_keeps_selection(g_pluginWindow, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 15;
    }
    if (repeatCount > 1 && !editor_commands_work(g_pluginWindow, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 7;
    }
    if (repeatCount > 1 && !editor_text_input_works(g_pluginWindow, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 9;
    }
    if (repeatCount > 1) {
        const int markdownFailure = markdown_editing_failure(g_pluginWindow, documentPath);
        if (markdownFailure != 0) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 20 + markdownFailure;
        }
    }
    if (repeatCount > 1 && !editor_replace_works(g_pluginWindow, documentPath)) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 10;
    }

    for (int iteration = 1; iteration < repeatCount; ++iteration) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        mutablePath = documentPath.wstring();
        g_pluginWindow = loadPlugin(host, mutablePath.data(), 0);
        if (!g_pluginWindow) {
            MessageBoxW(host, L"插件重复打开测试失败。", L"WLX Harness", MB_OK | MB_ICONERROR);
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 5;
        }
        if (!default_mode_is_correct(g_pluginWindow, documentPath)) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 6;
        }
        if (!editor_commands_work(g_pluginWindow, documentPath)) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 7;
        }
        if (!editor_text_input_works(g_pluginWindow, documentPath)) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 9;
        }
        const int markdownFailure = markdown_editing_failure(g_pluginWindow, documentPath);
        if (markdownFailure != 0) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 20 + markdownFailure;
        }
        if (!editor_replace_works(g_pluginWindow, documentPath)) {
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
            DestroyWindow(host);
            FreeLibrary(g_plugin);
            if (SUCCEEDED(comResult)) CoUninitialize();
            return 10;
        }
    }

    if (repeatCount > 1) {
        g_closePlugin(g_pluginWindow);
        g_pluginWindow = nullptr;
        if (is_lisp_path(documentPath)) {
            // Reproduce Total Commander's in-process uninstall/reinstall path:
            // unload the DLL completely, then load the same plugin again
            // without terminating the host process.
            if (!FreeLibrary(g_plugin)) {
                DestroyWindow(host);
                if (SUCCEEDED(comResult)) CoUninitialize();
                return 11;
            }
            g_plugin = nullptr;
            g_closePlugin = nullptr;
            g_plugin = LoadLibraryW(pluginPath.c_str());
            if (!g_plugin) {
                DestroyWindow(host);
                if (SUCCEEDED(comResult)) CoUninitialize();
                return 12;
            }
            loadPlugin = reinterpret_cast<ListLoadWFn>(GetProcAddress(g_plugin, "ListLoadW"));
            g_closePlugin = reinterpret_cast<ListCloseWindowFn>(GetProcAddress(g_plugin, "ListCloseWindow"));
            if (!loadPlugin || !g_closePlugin) {
                DestroyWindow(host);
                FreeLibrary(g_plugin);
                g_plugin = nullptr;
                if (SUCCEEDED(comResult)) CoUninitialize();
                return 13;
            }
            mutablePath = documentPath.wstring();
            g_pluginWindow = loadPlugin(host, mutablePath.data(), 0);
            if (!g_pluginWindow || !default_mode_is_correct(g_pluginWindow, documentPath)) {
                if (g_pluginWindow) g_closePlugin(g_pluginWindow);
                g_pluginWindow = nullptr;
                DestroyWindow(host);
                FreeLibrary(g_plugin);
                g_plugin = nullptr;
                if (SUCCEEDED(comResult)) CoUninitialize();
                return 14;
            }
            g_closePlugin(g_pluginWindow);
            g_pluginWindow = nullptr;
        }
        DestroyWindow(host);
        FreeLibrary(g_plugin);
        g_plugin = nullptr;
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    SetWindowTextW(host, L"EditMdView — WLX 预览");
    RECT client{};
    GetClientRect(host, &client);
    MoveWindow(g_pluginWindow, 0, 0, client.right, client.bottom, TRUE);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_plugin) FreeLibrary(g_plugin);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
