#include "app_window.hpp"

#include "html_preview.hpp"
#include "i18n.hpp"
#include "markdown.hpp"
#include "wlx_api.h"

#include <Scintilla.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace editmdview {
namespace {

constexpr wchar_t kWindowClass[] = L"EditMdView.PluginWindow";
constexpr UINT_PTR kPreviewTimer = 1;
constexpr UINT_PTR kPreviewSyncTimer = 2;
constexpr UINT_PTR kExternalChangeTimer = 3;
constexpr UINT_PTR kRecoveryTimer = 4;
constexpr UINT_PTR kConfigurationStatusTimer = 5;
constexpr UINT kPreviewDelayMs = 220;
constexpr UINT kPreviewSyncDelayMs = 35;
constexpr UINT kExternalChangeDelayMs = 1200;
constexpr UINT kRecoveryDelayMs = 1500;
constexpr UINT kConfigurationStatusDelayMs = 2500;
constexpr UINT kMessageSave = WM_APP + 1;
constexpr UINT kMessageToggleMode = WM_APP + 2;
constexpr UINT kMessageFocusFind = WM_APP + 3;
constexpr UINT kMessageFindNext = WM_APP + 4;
constexpr UINT kMessageGoToLine = WM_APP + 5;
constexpr UINT kMessageEditorMenu = WM_APP + 6;
constexpr UINT kMessageZoom = WM_APP + 7;
constexpr UINT kMessageToggleWrap = WM_APP + 8;
constexpr UINT kMessageReplace = WM_APP + 9;
constexpr UINT kMessagePreviewLocate = WM_APP + 10;
constexpr UINT kMessageSetEol = WM_APP + 11;
constexpr UINT kMessageSetLanguage = WM_APP + 12;
constexpr UINT kMessageMarkdownFormat = WM_APP + 13;
constexpr UINT kMessageMarkdownSlash = WM_APP + 14;
constexpr UINT kMessagePreviewFindResult = WM_APP + 15;
constexpr UINT kMessagePreviewReady = WM_APP + 16;
constexpr UINT kMessageReloadConfiguration = WM_APP + 17;
constexpr UINT kMessageRestoreEditorView = WM_APP + 18;
constexpr UINT kMessageSaveAs = WM_APP + 19;

constexpr int kIdEdit = 1001;
constexpr int kIdSplit = 1002;
constexpr int kIdPreview = 1003;
constexpr int kIdSave = 1004;
constexpr int kIdTheme = 1005;
constexpr int kIdFindBox = 1006;
constexpr int kIdFindNext = 1007;
constexpr int kIdMore = 1008;
constexpr int kIdReplace = 1009;
constexpr int kIdSplitDivider = 1010;
constexpr int kIdLanguage = 1011;

constexpr int kMenuUndo = 2001;
constexpr int kMenuRedo = 2002;
constexpr int kMenuCut = 2003;
constexpr int kMenuCopy = 2004;
constexpr int kMenuPaste = 2005;
constexpr int kMenuDelete = 2006;
constexpr int kMenuSelectAll = 2007;
constexpr int kMenuGoToLine = 2008;
constexpr int kMenuWrap = 2009;
constexpr int kMenuZoomIn = 2010;
constexpr int kMenuZoomOut = 2011;
constexpr int kMenuZoomReset = 2012;
constexpr int kMenuReplace = 2013;
constexpr int kMenuReloadConfiguration = 2014;
constexpr int kMenuSave = 2015;
constexpr int kMenuSaveAs = 2016;

constexpr int kMenuEolCrLf = 4101;
constexpr int kMenuEolLf = 4102;
constexpr int kMenuEolCr = 4103;
constexpr int kMenuLanguageBase = 4200;
constexpr int kMenuUiLanguageBase = 4400;

constexpr int kStatusCaret = 0;
constexpr int kStatusLanguage = 1;
constexpr int kStatusEncoding = 2;
constexpr int kStatusEol = 3;
constexpr int kStatusState = 4;

constexpr std::array<std::pair<SyntaxLanguage, const wchar_t*>, 21> kLanguages = {{
    {SyntaxLanguage::Plain, L"Plain text"},
    {SyntaxLanguage::Markdown, L"Markdown"},
    {SyntaxLanguage::Cpp, L"C/C++/Java"},
    {SyntaxLanguage::JavaScript, L"JavaScript/TypeScript"},
    {SyntaxLanguage::Python, L"Python"},
    {SyntaxLanguage::Json, L"JSON"},
    {SyntaxLanguage::Html, L"HTML"},
    {SyntaxLanguage::Xml, L"XML"},
    {SyntaxLanguage::Css, L"CSS"},
    {SyntaxLanguage::Bash, L"Shell"},
    {SyntaxLanguage::Sql, L"SQL"},
    {SyntaxLanguage::Yaml, L"YAML"},
    {SyntaxLanguage::Properties, L"Properties"},
    {SyntaxLanguage::Conf, L"Apache config"},
    {SyntaxLanguage::CMake, L"CMake"},
    {SyntaxLanguage::Makefile, L"Makefile"},
    {SyntaxLanguage::Batch, L"Batch"},
    {SyntaxLanguage::PowerShell, L"PowerShell"},
    {SyntaxLanguage::Rust, L"Rust"},
    {SyntaxLanguage::Lua, L"Lua"},
    {SyntaxLanguage::Lisp, L"Lisp/Scheme"},
}};

constexpr wchar_t kReplaceWindowClass[] = L"EditMdView.Replace";
constexpr int kReplaceFind = 3101;
constexpr int kReplaceWith = 3102;
constexpr int kReplaceMatchCase = 3103;
constexpr int kReplaceWholeWord = 3104;
constexpr int kReplaceDirectionUp = 3105;
constexpr int kReplaceFindNext = 3106;
constexpr int kReplaceOne = 3107;
constexpr int kReplaceAll = 3108;
constexpr int kReplaceClose = 3109;
constexpr int kReplaceResult = 3110;
constexpr std::size_t kHistoryLimit = 20;

constexpr wchar_t kSlashWindowClass[] = L"EditMdView.MarkdownSlash";
constexpr int kSlashRows = 5101;
constexpr int kSlashColumns = 5102;
constexpr int kSlashInsert = 5103;
constexpr int kSlashItemHeight = 52;
constexpr int kSlashPopupWidth = 310;
constexpr int kSlashMaxVisible = 8;

struct SlashCommand {
    const char* id;
    const wchar_t* glyph;
    const wchar_t* label;
    const wchar_t* description;
    const wchar_t* aliases;
};

constexpr std::array<SlashCommand, 15> kSlashCommands = {{
    {"h1", L"H1", L"Heading 1", L"Large heading", L"h1 heading 标题1 一级"},
    {"h2", L"H2", L"Heading 2", L"Medium heading", L"h2 heading 标题2 二级"},
    {"h3", L"H3", L"Heading 3", L"Small heading", L"h3 heading 标题3 三级"},
    {"l1", L"•", L"Bulleted list", L"Bulleted list", L"l1 ul bullet list 列表"},
    {"l2", L"1.", L"Numbered list", L"Numbered list", L"l2 ol numbered list 编号"},
    {"l3", L"☑", L"Task list", L"Task list with checkboxes", L"l3 todo task checklist 任务"},
    {"code", L"</>", L"Code block", L"Fenced code block", L"code codeblock 代码"},
    {"quote", L"❞", L"Quote", L"Quoted text", L"quote blockquote 引用"},
    {"table", L"▦", L"Table", L"Choose rows and columns", L"table grid 表格"},
    {"v1", L"i", L"Note", L"Note callout", L"v1 note info 注释"},
    {"v2", L"!", L"Important", L"Important callout", L"v2 important 重要"},
    {"v3", L"✦", L"Tip", L"Tip callout", L"v3 tip hint 提示"},
    {"v4", L"△", L"Warning", L"Warning callout", L"v4 warning 注意"},
    {"v5", L"!", L"Caution", L"Caution callout", L"v5 caution danger 警告"},
    {"link", L"↗", L"Internal link", L"Insert a Markdown link", L"link url href 链接"},
}};

std::wstring lower_wide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::string escape_history(std::wstring_view value) {
    const std::string utf8 = wide_to_utf8(value);
    std::string result;
    result.reserve(utf8.size());
    for (const char character : utf8) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '\t': result += "\\t"; break;
        case '\r': result += "\\r"; break;
        case '\n': result += "\\n"; break;
        default: result += character; break;
        }
    }
    return result;
}

std::wstring unescape_history(std::string_view value) {
    std::string utf8;
    utf8.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 >= value.size()) {
            utf8 += value[index];
            continue;
        }
        switch (value[++index]) {
        case 't': utf8 += '\t'; break;
        case 'r': utf8 += '\r'; break;
        case 'n': utf8 += '\n'; break;
        case '\\': utf8 += '\\'; break;
        default:
            utf8 += '\\';
            utf8 += value[index];
            break;
        }
    }
    return utf8_to_wide(utf8);
}

std::wstring window_text(HWND window) {
    if (!window) return {};
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

bool system_dark_mode() {
    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &size) != ERROR_SUCCESS) {
        return false;
    }
    return lightTheme == 0;
}

HFONT create_ui_font(HWND window) {
    HDC device = GetDC(window);
    const int dpi = device ? GetDeviceCaps(device, LOGPIXELSY) : 96;
    if (device) ReleaseDC(window, device);
    LOGFONTW font{};
    font.lfHeight = -MulDiv(9, dpi > 0 ? dpi : 96, 72);
    font.lfWeight = FW_NORMAL;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    font.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    wcscpy_s(font.lfFaceName, L"MS Shell Dlg");
    return CreateFontIndirectW(&font);
}

HWND make_button(HWND parent, HINSTANCE instance, int id, const wchar_t* text, DWORD buttonStyle = BS_PUSHBUTTON) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | buttonStyle,
        0, 0, 72, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}

std::wstring file_title(const std::filesystem::path& path) {
    const auto filename = path.filename().wstring();
    return filename.empty() ? path.wstring() : filename;
}

bool prompt_for_save_path(HWND owner, const std::filesystem::path& currentPath,
    std::filesystem::path& selectedPath, std::wstring& error) {
    std::wstring filters;
    const auto appendFilter = [&filters](const std::wstring& label, const wchar_t* pattern) {
        filters += label;
        filters.push_back(L'\0');
        filters += pattern;
        filters.push_back(L'\0');
    };
    appendFilter(i18n::text(L"Markdown documents") + L" (*.md;*.markdown)", L"*.md;*.markdown");
    appendFilter(i18n::text(L"HTML documents") + L" (*.html;*.htm)", L"*.html;*.htm");
    appendFilter(i18n::text(L"Text and code files") +
        L" (*.txt;*.log;*.ini;*.json;*.xml;*.yaml;*.cpp;*.lsp)",
        L"*.txt;*.log;*.ini;*.cfg;*.conf;*.json;*.xml;*.yaml;*.yml;*.csv;*.cpp;*.c;*.h;*.hpp;*.py;*.js;*.ts;*.css;*.lsp");
    appendFilter(i18n::text(L"All files") + L" (*.*)", L"*.*");
    filters.push_back(L'\0');

    std::vector<wchar_t> fileBuffer(32768, L'\0');
    const std::wstring current = currentPath.wstring();
    if (current.size() >= fileBuffer.size()) {
        error = i18n::text(L"The current path is too long for the Save As dialog.");
        return false;
    }
    std::copy(current.begin(), current.end(), fileBuffer.begin());

    std::wstring extension = lower_wide(currentPath.extension().wstring());
    if (!extension.empty() && extension.front() == L'.') extension.erase(extension.begin());
    if (extension.empty()) extension = L"txt";
    DWORD filterIndex = 3;
    if (extension == L"md" || extension == L"markdown") filterIndex = 1;
    else if (extension == L"html" || extension == L"htm") filterIndex = 2;

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filters.c_str();
    dialog.nFilterIndex = filterIndex;
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    const std::wstring dialogTitle = i18n::text(L"Save As");
    dialog.lpstrTitle = dialogTitle.c_str();
    dialog.lpstrDefExt = extension.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&dialog)) {
        selectedPath = std::filesystem::path(fileBuffer.data());
        error.clear();
        return true;
    }
    const DWORD dialogError = CommDlgExtendedError();
    if (dialogError != 0) {
        error = i18n::text(L"Unable to open the Save As dialog (error code") + L" " +
            std::to_wstring(dialogError) + L"）。";
    } else {
        error.clear();
    }
    return false;
}

std::filesystem::path module_path(HINSTANCE instance) {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(instance, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::wstring windows_error_message(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count && buffer ? std::wstring(buffer, count) : i18n::text(L"Unknown error");
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}

bool run_elevated_save_helper(HWND owner, HINSTANCE instance,
    const std::filesystem::path& stagedFile, const std::filesystem::path& targetFile,
    std::wstring& error) {
    const std::filesystem::path helper = module_path(instance).parent_path() / L"EditMdViewSave.exe";
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(helper, fileError)) {
        error = i18n::text(L"The protected folder requires administrator access, but EditMdViewSave.exe is missing. Reinstall the complete plugin package.");
        return false;
    }

    // Double quotes cannot occur in Windows file names, so quoting both full
    // paths is sufficient for CommandLineToArgvW in the helper process.
    const std::wstring parameters = L"--replace \"" + stagedFile.wstring() + L"\" \"" +
        targetFile.wstring() + L"\"";
    const std::wstring workingDirectory = helper.parent_path().wstring();
    SHELLEXECUTEINFOW execution{sizeof(execution)};
    execution.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    execution.hwnd = GetAncestor(owner, GA_ROOT);
    execution.lpVerb = L"runas";
    execution.lpFile = helper.c_str();
    execution.lpParameters = parameters.c_str();
    execution.lpDirectory = workingDirectory.c_str();
    execution.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execution)) {
        const DWORD launchError = GetLastError();
        error = launchError == ERROR_CANCELLED
            ? i18n::text(L"The administrator request was cancelled. The file was not saved.")
            : i18n::text(L"Unable to start the elevated save helper:") + L" " + windows_error_message(launchError);
        return false;
    }
    if (!execution.hProcess) {
        error = i18n::text(L"The elevated save helper did not return a process handle.");
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(execution.hProcess, INFINITE);
    DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    DWORD exitCode = ERROR_GEN_FAILURE;
    bool readExitCode = false;
    if (waitResult == WAIT_OBJECT_0) {
        readExitCode = GetExitCodeProcess(execution.hProcess, &exitCode) != FALSE;
        if (!readExitCode) waitError = GetLastError();
    }
    CloseHandle(execution.hProcess);
    if (!readExitCode) {
        error = i18n::text(L"An error occurred while waiting for the elevated save helper:") + L" " + windows_error_message(waitError);
        return false;
    }
    if (exitCode != ERROR_SUCCESS) {
        error = i18n::text(L"Elevated save failed:") + L" " + windows_error_message(exitCode);
        return false;
    }
    return true;
}

constexpr wchar_t kGoToLineClass[] = L"EditMdView.GoToLine";
constexpr int kGoToLineEdit = 3001;

struct GoToLineState {
    int result = 0;
};

LRESULT CALLBACK go_to_line_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<GoToLineState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<GoToLineState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_COMMAND && state) {
        if (LOWORD(wParam) == IDOK) {
            wchar_t value[32]{};
            GetWindowTextW(GetDlgItem(window, kGoToLineEdit), value, static_cast<int>(std::size(value)));
            wchar_t* end = nullptr;
            const long line = std::wcstol(value, &end, 10);
            if (line > 0 && line <= std::numeric_limits<int>::max() && end != value && *end == L'\0') {
                state->result = static_cast<int>(line);
                DestroyWindow(window);
            } else {
                MessageBeep(MB_ICONWARNING);
                SetFocus(GetDlgItem(window, kGoToLineEdit));
            }
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int prompt_for_line(HWND owner, HINSTANCE instance, HFONT uiFont, int currentLine) {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = go_to_line_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kGoToLineClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;

    HWND root = GetAncestor(owner, GA_ROOT);
    RECT ownerBounds{};
    GetWindowRect(root, &ownerBounds);
    RECT bounds{0, 0, 320, 142};
    AdjustWindowRectEx(&bounds, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = ownerBounds.left + ((ownerBounds.right - ownerBounds.left) - width) / 2;
    const int y = ownerBounds.top + ((ownerBounds.bottom - ownerBounds.top) - height) / 2;

    GoToLineState state;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kGoToLineClass,
        i18n::text(L"Go to Line").c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
        root, nullptr, instance, &state);
    if (!dialog) return 0;

    HWND label = CreateWindowExW(0, L"STATIC", i18n::text(L"Line:").c_str(), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        18, 18, 60, 26, dialog, nullptr, instance, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        ES_NUMBER | ES_AUTOHSCROLL, 78, 18, 216, 26, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGoToLineEdit)), instance, nullptr);
    HWND ok = CreateWindowExW(0, L"BUTTON", i18n::text(L"OK").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        132, 62, 76, 28, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), instance, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", i18n::text(L"Cancel").c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        218, 62, 76, 28, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), instance, nullptr);
    const HFONT font = uiFont ? uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (HWND control : {label, edit, ok, cancel}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowTextW(edit, std::to_wstring(currentLine).c_str());
    SendMessageW(edit, EM_SETSEL, 0, -1);

    EnableWindow(root, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    SetFocus(edit);
    MSG message{};
    bool repostQuit = false;
    while (IsWindow(dialog)) {
        const BOOL received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            repostQuit = received == 0;
            break;
        }
        if (message.message == WM_KEYDOWN && message.hwnd == edit) {
            if (message.wParam == VK_RETURN) {
                SendMessageW(dialog, WM_COMMAND, IDOK, 0);
                continue;
            }
            if (message.wParam == VK_ESCAPE) {
                SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
                continue;
            }
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(root, TRUE);
    SetForegroundWindow(root);
    if (repostQuit) PostQuitMessage(static_cast<int>(message.wParam));
    return state.result;
}

} // namespace

struct PreviewRenderWorker {
    struct Request {
        std::uint64_t generation = 0;
        bool html = false;
        bool dark = false;
        std::string source;
        std::string title;
        std::filesystem::path folder;
        std::optional<double> scrollFraction;
    };

    struct Result {
        std::wstring html;
        std::filesystem::path folder;
        std::optional<double> scrollFraction;
    };

    explicit PreviewRenderWorker(HWND targetWindow) : target(targetWindow), worker([this] { run(); }) {}

    ~PreviewRenderWorker() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            ++latestGeneration;
            pending.reset();
            completed.reset();
        }
        condition.notify_one();
        if (worker.joinable()) worker.join();
    }

    void submit(Request request) {
        {
            std::lock_guard lock(mutex);
            request.generation = ++latestGeneration;
            pending = std::move(request);
            completed.reset();
        }
        condition.notify_one();
    }

    void cancel() {
        std::lock_guard lock(mutex);
        ++latestGeneration;
        pending.reset();
        completed.reset();
    }

    std::optional<Result> take_completed() {
        std::lock_guard lock(mutex);
        if (!completed) return std::nullopt;
        std::optional<Result> result = std::move(completed);
        completed.reset();
        return result;
    }

private:
    void run() {
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return stopping || pending.has_value(); });
                if (stopping) return;
                request = std::move(*pending);
                pending.reset();
            }

            Result result;
            result.folder = request.folder;
            result.scrollFraction = request.scrollFraction;
            try {
                if (request.html) {
                    result.html = render_html_preview(request.source, "https://editmdview.local/");
                } else {
                    MarkdownRenderOptions options;
                    options.dark = request.dark;
                    options.baseHref = "https://editmdview.local/";
                    options.title = request.title;
                    result.html = render_markdown_html(request.source, options);
                }
            } catch (...) {
                result.html = L"<!doctype html><meta charset=utf-8><style>body{font:16px Segoe UI;padding:32px}</style><p>" +
                    i18n::text(L"Preview generation failed in the background. Editing and saving remain available.") + L"</p>";
            }

            {
                std::lock_guard lock(mutex);
                if (stopping) return;
                if (request.generation != latestGeneration) continue;
                completed = std::move(result);
            }
            PostMessageW(target, kMessagePreviewReady, 0, 0);
        }
    }

    HWND target = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    std::uint64_t latestGeneration = 0;
    std::optional<Request> pending;
    std::optional<Result> completed;
    std::thread worker;
};

bool AppWindow::register_class(HINSTANCE instance) {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = window_proc;
    windowClass.cbWndExtra = sizeof(void*);
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    WNDCLASSEXW slashClass{sizeof(slashClass)};
    slashClass.style = CS_DBLCLKS;
    slashClass.lpfnWndProc = slash_window_proc;
    slashClass.cbWndExtra = sizeof(void*);
    slashClass.hInstance = instance;
    slashClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    slashClass.lpszClassName = kSlashWindowClass;
    return RegisterClassExW(&slashClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void AppWindow::unregister_classes(HINSTANCE instance) noexcept {
    // These classes all contain callbacks into this DLL, so none may outlive
    // a dynamic unload/reload cycle in the Total Commander process.
    UnregisterClassW(kReplaceWindowClass, instance);
    UnregisterClassW(kGoToLineClass, instance);
    UnregisterClassW(kSlashWindowClass, instance);
    UnregisterClassW(kWindowClass, instance);
}

HWND AppWindow::create(HWND parent, HINSTANCE instance, const std::filesystem::path& path, int) {
    std::wstring error;
    i18n::initialize(module_path(instance));
    Document document;
    if (!document.load(path, error)) {
        MessageBoxW(parent, error.c_str(), L"EditMdView", MB_OK | MB_ICONERROR);
        return nullptr;
    }

    if (!register_class(instance)) return nullptr;
    RECT bounds{};
    GetClientRect(parent, &bounds);
    auto app = std::unique_ptr<AppWindow>(new AppWindow(instance, std::move(document)));
    AppWindow* rawApp = app.release();
    HWND window = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, bounds.right, bounds.bottom, parent, nullptr, instance, rawApp);
    if (!window) return nullptr;
    return window;
}

AppWindow* AppWindow::from(HWND window) noexcept {
    return reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

AppWindow::AppWindow(HINSTANCE instance, Document document)
    : instance_(instance), document_(std::move(document)), dark_(system_dark_mode()),
      runtimeDataDirectory_(runtime_data_directory(module_path(instance))) {}

AppWindow::~AppWindow() {
    previewRenderWorker_.reset();
    if (slashPopup_ && IsWindow(slashPopup_)) DestroyWindow(slashPopup_);
    if (replaceDialog_ && IsWindow(replaceDialog_)) DestroyWindow(replaceDialog_);
    if (editor_.handle()) RemoveWindowSubclass(editor_.handle(), editor_subclass_proc, 1);
    if (toolbar_) RemoveWindowSubclass(toolbar_, toolbar_subclass_proc, 3);
    if (status_) RemoveWindowSubclass(status_, status_subclass_proc, 5);
    if (splitDivider_) RemoveWindowSubclass(splitDivider_, divider_subclass_proc, 6);
    if (findEdit_) RemoveWindowSubclass(findEdit_, find_subclass_proc, 2);
    preview_.destroy();
    editor_.destroy();
    if (comInitialized_) CoUninitialize();
    if (uiFont_) DeleteObject(uiFont_);
}

bool AppWindow::initialize(HWND window) {
    window_ = window;
    uiFont_ = create_ui_font(window_);
    load_search_history();
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&commonControls);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = SUCCEEDED(comResult);
    toolbar_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 36, window_, nullptr, instance_, nullptr);
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_TOOLTIPS,
        0, 0, 100, 24, window_, nullptr, instance_, nullptr);
    splitDivider_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_NOTIFY,
        0, 0, 8, 100, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSplitDivider)), instance_, nullptr);
    if (!toolbar_ || !status_ || !splitDivider_) return false;
    SetWindowSubclass(toolbar_, toolbar_subclass_proc, 3, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(status_, status_subclass_proc, 5, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(splitDivider_, divider_subclass_proc, 6, reinterpret_cast<DWORD_PTR>(this));

    make_button(toolbar_, instance_, kIdEdit, i18n::text(L"Edit").c_str(), BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP);
    make_button(toolbar_, instance_, kIdSplit, i18n::text(L"Split").c_str(), BS_AUTORADIOBUTTON | BS_PUSHLIKE);
    make_button(toolbar_, instance_, kIdPreview, i18n::text(L"Preview").c_str(), BS_AUTORADIOBUTTON | BS_PUSHLIKE);
    make_button(toolbar_, instance_, kIdSave, i18n::text(L"Save").c_str());
    make_button(toolbar_, instance_, kIdTheme,
        dark_ ? i18n::text(L"Light").c_str() : i18n::text(L"Dark").c_str());
    make_button(toolbar_, instance_, kIdLanguage, i18n::text(L"Language").c_str());
    make_button(toolbar_, instance_, kIdMore, i18n::text(L"More").c_str());
    findBox_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWN | CBS_AUTOHSCROLL, 0, 0, 180, 220, toolbar_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFindBox)), instance_, nullptr);
    make_button(toolbar_, instance_, kIdFindNext, i18n::text(L"Find").c_str());
    make_button(toolbar_, instance_, kIdReplace, i18n::text(L"Replace").c_str());
    findEdit_ = FindWindowExW(findBox_, nullptr, L"Edit", nullptr);
    if (findEdit_) SetWindowSubclass(findEdit_, find_subclass_proc, 2, reinterpret_cast<DWORD_PTR>(this));

    HFONT font = uiFont_ ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(toolbar_, [](HWND child, LPARAM fontValue) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontValue), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    populate_history_combo(findBox_, findHistory_);
    previewRenderWorker_ = std::make_unique<PreviewRenderWorker>(window_);

    std::wstring error;
    auto properties = SciteProperties::load_for_document(
        module_path(instance_), document_.path(), dark_);
    configurationSignature_ = properties.configuration_signature();
    editor_.set_properties(std::move(properties));
    if (!editor_.create(window_, instance_, dark_, error)) {
        MessageBoxW(window_, error.c_str(), L"EditMdView", MB_OK | MB_ICONERROR);
        return false;
    }
    SetWindowSubclass(editor_.handle(), editor_subclass_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    restoringDocumentState_ = true;
    editor_.set_text(document_.text(), document_.path(), document_.eol_name());

    const auto restored = stored_document_view_state(document_.path());
    if (restored) splitRatio_ = std::clamp(restored->splitRatio, 0.1, 0.9);
    const bool recovered = offer_recovery_snapshot();

    if (document_.supports_preview()) {
        preview_.create(window_, instance_, error);
        preview_.set_find_shortcuts(window_, kMessageFocusFind, kMessageFindNext,
            kMessagePreviewFindResult, kMessageToggleMode, kMessageReloadConfiguration,
            kMessageSave, kMessageSaveAs);
        preview_.set_source_navigation(window_, kMessagePreviewLocate);
    }
    ViewMode initialMode = document_.supports_preview()
        ? (restored ? restored->mode : ViewMode::Preview)
        : ViewMode::Edit;
    if (recovered) initialMode = ViewMode::Edit;
    set_mode(initialMode);
    if (document_.supports_preview()) {
        refresh_preview(restored ? std::optional<double>(restored->previewScrollFraction)
                                 : std::optional<double>(0.0));
    }
    if (restored) {
        pendingEditorViewState_ = restored->editor;
        PostMessageW(window_, kMessageRestoreEditorView, 0, 0);
    }
    restoringDocumentState_ = false;
    update_status();
    SetTimer(window_, kExternalChangeTimer, kExternalChangeDelayMs, nullptr);
    return true;
}

LRESULT CALLBACK AppWindow::window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = from(window);
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_CREATE && !self->initialize(window)) return -1;
    const LRESULT result = self->handle_message(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete self;
    }
    return result;
}

LRESULT CALLBACK AppWindow::editor_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (message == WM_LBUTTONDOWN && self && self->slashPopup_ && !self->slashTableMode_) {
        self->hide_slash_popup();
    }
    if (message == WM_KILLFOCUS && self && self->slashPopup_ && !self->slashTableMode_) {
        const HWND next = reinterpret_cast<HWND>(wParam);
        if (next != self->slashPopup_ && !IsChild(self->slashPopup_, next)) self->hide_slash_popup();
    }
    if ((message == WM_CHAR || message == WM_SYSCHAR) && self) {
        if (self->suppressShortcutCharacter_) {
            self->suppressShortcutCharacter_ = false;
            if (message == WM_SYSCHAR || wParam < 0x20) return 0;
        }
        // Never insert orphaned C0 control characters. Backspace, Tab and
        // Enter remain available through Scintilla's normal key handling.
        if (message == WM_CHAR && wParam < 0x20 && wParam != L'\b' &&
            wParam != L'\t' && wParam != L'\r' && wParam != L'\n') return 0;
        if (message == WM_CHAR && wParam >= 0x20 && self->document_.is_markdown() &&
            self->mode_ != ViewMode::Preview &&
            self->editor_.handle_markdown_character(static_cast<int>(wParam))) {
            self->hide_slash_popup();
            self->update_status();
            return 0;
        }
    }
    if (message == WM_CONTEXTMENU && self) {
        PostMessageW(self->window_, kMessageEditorMenu, 0, 0);
        return 0;
    }
    if (message == WM_MOUSEWHEEL && self && self->slashPopup_ && !self->slashTableMode_) {
        RECT popupBounds{};
        GetWindowRect(self->slashPopup_, &popupBounds);
        const POINT pointer{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PtInRect(&popupBounds, pointer)) {
            self->scroll_slash_popup(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
    }
    if (message == WM_MOUSEWHEEL && self && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        const INT_PTR direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
        PostMessageW(self->window_, kMessageZoom, static_cast<WPARAM>(direction), 0);
        return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (control && wParam >= 'A' && wParam <= 'Z') self->suppressShortcutCharacter_ = true;
        if (self->slashPopup_ && !self->slashTableMode_ && !control && !alt) {
            if (wParam == VK_UP || wParam == VK_DOWN) {
                self->move_slash_selection(wParam == VK_UP ? -1 : 1);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_TAB) {
                // TranslateMessage will still emit WM_CHAR for the intercepted
                // key. Suppress that trailing CR/Tab so it is not inserted
                // after the Markdown template.
                self->suppressShortcutCharacter_ = true;
                self->execute_slash_selection();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->hide_slash_popup();
                return 0;
            }
        }
        if (self->document_.is_markdown() && self->mode_ != ViewMode::Preview && !control && !alt) {
            if (wParam == VK_RETURN && self->editor_.handle_markdown_enter()) {
                self->suppressShortcutCharacter_ = true;
                self->hide_slash_popup();
                self->update_status();
                return 0;
            }
            if (wParam == VK_TAB && self->editor_.handle_markdown_tab(shift)) {
                self->suppressShortcutCharacter_ = true;
                self->hide_slash_popup();
                self->update_status();
                return 0;
            }
        }
        if (self->document_.is_markdown() && self->mode_ != ViewMode::Preview && control && !alt) {
            if (!shift && wParam == 'V' && self->editor_.paste_markdown_smart()) {
                self->hide_slash_popup();
                self->update_status();
                return 0;
            }
            std::optional<MarkdownFormat> format;
            if (!shift && wParam == 'B') format = MarkdownFormat::Bold;
            else if (!shift && wParam == 'I') format = MarkdownFormat::Italic;
            else if (shift && wParam == 'H') format = MarkdownFormat::Highlight;
            else if (shift && wParam == 'X') format = MarkdownFormat::Strikethrough;
            else if (!shift && wParam == VK_OEM_3) format = MarkdownFormat::InlineCode;
            if (format && self->editor_.apply_markdown_format(*format)) {
                self->hide_slash_popup();
                self->update_status();
                return 0;
            }
            if (!shift && wParam == 'K' && self->editor_.insert_markdown_link()) {
                self->hide_slash_popup();
                self->update_status();
                return 0;
            }
        }
        if (control && wParam == 'S') {
            PostMessageW(self->window_, shift ? kMessageSaveAs : kMessageSave, 0, 0);
            return 0;
        }
        if (control && shift && wParam == 'R') {
            PostMessageW(self->window_, kMessageReloadConfiguration, 0, 0);
            return 0;
        }
        if (control && wParam == 'M') {
            PostMessageW(self->window_, kMessageToggleMode, 0, 0);
            return 0;
        }
        if (control && wParam == 'F') {
            PostMessageW(self->window_, kMessageFocusFind, 0, 0);
            return 0;
        }
        if (control && wParam == 'G') {
            PostMessageW(self->window_, kMessageGoToLine, 0, 0);
            return 0;
        }
        if (control && !shift && wParam == 'H') {
            PostMessageW(self->window_, kMessageReplace, 0, 0);
            return 0;
        }
        if (control && (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
            PostMessageW(self->window_, kMessageZoom, 1, 0);
            return 0;
        }
        if (control && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)) {
            PostMessageW(self->window_, kMessageZoom, static_cast<WPARAM>(static_cast<INT_PTR>(-1)), 0);
            return 0;
        }
        if (control && wParam == '0') {
            PostMessageW(self->window_, kMessageZoom, 0, 0);
            return 0;
        }
        if (alt && wParam == 'Z') {
            self->suppressShortcutCharacter_ = true;
            PostMessageW(self->window_, kMessageToggleWrap, 0, 0);
            return 0;
        }
        if (wParam == VK_F3) {
            PostMessageW(self->window_, kMessageFindNext, shift, 0);
            return 0;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::find_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (message == WM_SETFOCUS && self) self->prefill_find_from_selection();
    if (message == WM_KEYDOWN && self) {
        if (wParam == VK_DELETE && (GetKeyState(VK_SHIFT) & 0x8000) != 0 &&
            self->delete_selected_history(window)) {
            return 0;
        }
        if (wParam == VK_RETURN) {
            PostMessageW(self->window_, kMessageFindNext, (GetKeyState(VK_SHIFT) & 0x8000) != 0, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (self->mode_ == ViewMode::Preview) self->preview_.focus();
            else self->editor_.focus();
            return 0;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::replace_control_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (message == WM_KEYDOWN && self && self->replaceDialog_) {
        if (wParam == VK_DELETE && (GetKeyState(VK_SHIFT) & 0x8000) != 0 &&
            self->delete_selected_history(window)) {
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(self->replaceDialog_);
            return 0;
        }
        if (wParam == VK_TAB) {
            HWND tabControl = window;
            const HWND parent = GetParent(window);
            if (parent == self->replaceFindCombo_ || parent == self->replaceWithCombo_) tabControl = parent;
            const BOOL previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (HWND next = GetNextDlgTabItem(self->replaceDialog_, tabControl, previous)) SetFocus(next);
            return 0;
        }
        if (wParam == VK_RETURN) {
            int command = GetDlgCtrlID(window);
            if (GetParent(window) == self->replaceFindCombo_) command = kReplaceFindNext;
            if (GetParent(window) == self->replaceWithCombo_) command = kReplaceOne;
            if (command == kReplaceClose) DestroyWindow(self->replaceDialog_);
            else if (command == kReplaceFindNext || command == kReplaceOne || command == kReplaceAll) {
                self->perform_replace_action(command);
            } else {
                self->perform_replace_action(kReplaceFindNext);
            }
            return 0;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::replace_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_COMMAND) {
        const int command = LOWORD(wParam);
        if (command == kReplaceClose || command == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        if (command == kReplaceFindNext || command == kReplaceOne || command == kReplaceAll) {
            self->perform_replace_action(command);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        self->replaceDialog_ = nullptr;
        self->replaceFindCombo_ = nullptr;
        self->replaceWithCombo_ = nullptr;
        self->replaceResult_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::slash_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        self->paint_slash_popup(device);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!self->slashTableMode_) {
            const int index = self->slashScrollOffset_ + GET_Y_LPARAM(lParam) / kSlashItemHeight;
            if (index >= 0 && index < static_cast<int>(self->slashVisibleCommands_.size()) &&
                index != self->slashSelected_) {
                self->slashSelected_ = index;
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (!self->slashTableMode_) {
            self->scroll_slash_popup(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (!self->slashTableMode_) {
            const int index = self->slashScrollOffset_ + GET_Y_LPARAM(lParam) / kSlashItemHeight;
            if (index >= 0 && index < static_cast<int>(self->slashVisibleCommands_.size())) {
                self->slashSelected_ = index;
                self->execute_slash_selection();
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kSlashInsert) {
            self->insert_table_from_form();
            return 0;
        }
        break;
    case WM_NCDESTROY:
        self->slashPopup_ = nullptr;
        self->slashRowsEdit_ = nullptr;
        self->slashColumnsEdit_ = nullptr;
        self->slashInsertButton_ = nullptr;
        self->slashVisibleCommands_.clear();
        self->slashScrollOffset_ = 0;
        self->slashWheelRemainder_ = 0;
        self->slashTableMode_ = false;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::slash_control_subclass_proc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (message == WM_KEYDOWN && self) {
        if (wParam == VK_ESCAPE) {
            self->hide_slash_popup(true);
            return 0;
        }
        if (wParam == VK_RETURN) {
            self->insert_table_from_form();
            return 0;
        }
        if (wParam == VK_TAB) {
            const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND next = nullptr;
            if (window == self->slashRowsEdit_) next = backwards
                ? self->slashInsertButton_ : self->slashColumnsEdit_;
            else if (window == self->slashColumnsEdit_) next = backwards
                ? self->slashRowsEdit_ : self->slashInsertButton_;
            else next = backwards ? self->slashColumnsEdit_ : self->slashRowsEdit_;
            if (next) SetFocus(next);
            return 0;
        }
        if ((wParam == VK_UP || wParam == VK_DOWN) &&
            (window == self->slashRowsEdit_ || window == self->slashColumnsEdit_)) {
            wchar_t value[8]{};
            GetWindowTextW(window, value, static_cast<int>(std::size(value)));
            int number = _wtoi(value);
            number = std::clamp(number + (wParam == VK_UP ? 1 : -1),
                window == self->slashRowsEdit_ ? 2 : 1, 20);
            SetWindowTextW(window, std::to_wstring(number).c_str());
            SendMessageW(window, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::toolbar_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (message == WM_COMMAND && self) {
        return SendMessageW(self->window_, message, wParam, lParam);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::status_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    const auto part_at = [window](POINT point) {
        const int count = static_cast<int>(SendMessageW(window, SB_GETPARTS, 0, 0));
        for (int part = 0; part < count; ++part) {
            RECT bounds{};
            if (SendMessageW(window, SB_GETRECT, part, reinterpret_cast<LPARAM>(&bounds)) &&
                PtInRect(&bounds, point)) return part;
        }
        return -1;
    };
    const auto wrap_indicator_hit = [window, self, &part_at](POINT point) {
        if (!self || part_at(point) != kStatusState) return false;
        RECT bounds{};
        if (!SendMessageW(window, SB_GETRECT, kStatusState, reinterpret_cast<LPARAM>(&bounds))) return false;
        const std::wstring label = self->editor_.wrap_enabled() ?
            i18n::text(L"Word wrap") : i18n::text(L"No wrap");
        HDC device = GetDC(window);
        if (!device) return false;
        const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        const HGDIOBJ previous = font ? SelectObject(device, font) : nullptr;
        SIZE extent{};
        GetTextExtentPoint32W(device, label.c_str(), static_cast<int>(label.size()), &extent);
        if (previous) SelectObject(device, previous);
        ReleaseDC(window, device);
        return point.x >= bounds.left && point.x <= bounds.left + extent.cx + 12;
    };
    if (message == WM_LBUTTONUP && self) {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int part = part_at(point);
        if (part == kStatusLanguage || part == kStatusEol) {
            self->show_status_menu(part);
            return 0;
        }
        if (wrap_indicator_hit(point)) {
            SendMessageW(self->window_, kMessageToggleWrap, 0, 0);
            return 0;
        }
    }
    if (message == WM_SETCURSOR) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        const int part = part_at(point);
        if (part == kStatusLanguage || part == kStatusEol || wrap_indicator_hit(point)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::divider_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<AppWindow*>(referenceData);
    if (!self) return DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const COLORREF background = self->dark_ ? RGB(30, 30, 30) : GetSysColor(COLOR_WINDOW);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(device, &bounds, backgroundBrush);
        DeleteObject(backgroundBrush);
        const int center = (bounds.left + bounds.right) / 2;
        RECT line{center - 1, bounds.top, center + 1, bounds.bottom};
        const COLORREF lineColor = (self->splitDragging_ || self->splitHover_)
            ? GetSysColor(COLOR_HIGHLIGHT)
            : (self->dark_ ? RGB(74, 74, 74) : RGB(210, 214, 220));
        HBRUSH lineBrush = CreateSolidBrush(lineColor);
        FillRect(device, &line, lineBrush);
        DeleteObject(lineBrush);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
    case WM_MOUSEMOVE: {
        if (!self->splitHover_) {
            self->splitHover_ = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        if (self->splitDragging_) {
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            MapWindowPoints(window, self->window_, &cursor, 1);
            RECT client{};
            GetClientRect(self->window_, &client);
            const int width = std::max(1, static_cast<int>(client.right - client.left));
            const double minimum = std::min(0.45, 180.0 / static_cast<double>(width));
            self->splitRatio_ = std::clamp(cursor.x / static_cast<double>(width), minimum, 1.0 - minimum);
            self->layout();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        self->splitHover_ = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        self->splitDragging_ = true;
        SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
        self->splitRatio_ = 0.5;
        self->layout();
        return 0;
    case WM_LBUTTONUP:
        if (self->splitDragging_) {
            self->splitDragging_ = false;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        self->splitDragging_ = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT AppWindow::handle_message(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        layout();
        restore_pending_editor_view_state();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification && notification->hwndFrom == editor_.handle()) {
            const auto code = notification->code;
            if (code == SCN_MODIFIED) {
                KillTimer(window_, kPreviewTimer);
                SetTimer(window_, kPreviewTimer, kPreviewDelayMs, nullptr);
                update_slash_popup();
                const auto* scintillaNotification = reinterpret_cast<const SCNotification*>(lParam);
                if (!restoringDocumentState_ &&
                    (scintillaNotification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
                    KillTimer(window_, kRecoveryTimer);
                    SetTimer(window_, kRecoveryTimer, kRecoveryDelayMs, nullptr);
                }
            }
            if (code == SCN_MARGINCLICK) {
                const auto* scintillaNotification = reinterpret_cast<const SCNotification*>(lParam);
                if (scintillaNotification->margin == 2) {
                    editor_.toggle_fold_at(scintillaNotification->position);
                }
            }
            if (code == SCN_CHARADDED) {
                const auto* scintillaNotification = reinterpret_cast<const SCNotification*>(lParam);
                editor_.character_added(scintillaNotification->ch);
                update_slash_popup();
            }
            if (code == SCN_UPDATEUI || code == SCN_SAVEPOINTLEFT || code == SCN_SAVEPOINTREACHED) {
                if (code == SCN_SAVEPOINTREACHED && !restoringDocumentState_) {
                    KillTimer(window_, kRecoveryTimer);
                    recoverySnapshotContent_.clear();
                    recoveryWriteFailed_ = false;
                    remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
                } else if (code == SCN_SAVEPOINTLEFT && !restoringDocumentState_) {
                    KillTimer(window_, kRecoveryTimer);
                    SetTimer(window_, kRecoveryTimer, kRecoveryDelayMs, nullptr);
                }
                if (code == SCN_UPDATEUI) {
                    editor_.update_ui();
                    if (slashPopup_) update_slash_popup();
                    const auto* scintillaNotification = reinterpret_cast<const SCNotification*>(lParam);
                    const int updated = scintillaNotification->updated;
                    if (!locatingFromPreview_ && !restoringDocumentState_) {
                        if ((updated & SC_UPDATE_SELECTION) != 0) schedule_preview_sync(true);
                        else if ((updated & SC_UPDATE_V_SCROLL) != 0) schedule_preview_sync(false);
                    }
                }
                update_status();
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kPreviewTimer) {
            KillTimer(window_, kPreviewTimer);
            refresh_preview();
        }
        if (wParam == kPreviewSyncTimer) {
            KillTimer(window_, kPreviewSyncTimer);
            sync_preview_to_editor();
        }
        if (wParam == kExternalChangeTimer) {
            check_external_change();
            check_configuration_change();
        }
        if (wParam == kRecoveryTimer) {
            KillTimer(window_, kRecoveryTimer);
            write_recovery_snapshot();
        }
        if (wParam == kConfigurationStatusTimer) {
            KillTimer(window_, kConfigurationStatusTimer);
            configurationStatus_.clear();
            update_status();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdEdit: set_mode(ViewMode::Edit); return 0;
        case kIdSplit: set_mode(ViewMode::Split); return 0;
        case kIdPreview: set_mode(ViewMode::Preview); return 0;
        case kIdSave: save(); return 0;
        case kIdTheme: toggle_theme(); return 0;
        case kIdLanguage: {
            RECT button{};
            GetWindowRect(GetDlgItem(toolbar_, kIdLanguage), &button);
            show_language_menu(POINT{button.left, button.bottom});
            return 0;
        }
        case kIdMore: {
            RECT button{};
            GetWindowRect(GetDlgItem(toolbar_, kIdMore), &button);
            show_editor_menu(POINT{button.left, button.bottom}, false);
            return 0;
        }
        case kIdFindNext: perform_find(false, false); return 0;
        case kIdReplace: show_replace_dialog(); return 0;
        default: break;
        }
        break;
    case kMessageSave:
        save();
        return 0;
    case kMessageSaveAs:
        save_as();
        return 0;
    case kMessageToggleMode:
        if (document_.supports_preview()) {
            switch (mode_) {
            case ViewMode::Preview: set_mode(ViewMode::Edit); break;
            case ViewMode::Edit: set_mode(ViewMode::Split); break;
            case ViewMode::Split: set_mode(ViewMode::Preview); break;
            }
        }
        return 0;
    case kMessageFocusFind:
        prefill_find_from_selection();
        SetFocus(findEdit_ ? findEdit_ : findBox_);
        if (findEdit_) SendMessageW(findEdit_, EM_SETSEL, 0, -1);
        return 0;
    case kMessageFindNext:
        perform_find(wParam != 0, false);
        return 0;
    case kMessageGoToLine:
        show_go_to_line();
        return 0;
    case kMessageReplace:
        show_replace_dialog();
        return 0;
    case kMessagePreviewLocate:
        if (mode_ == ViewMode::Split) {
            const int lineCount = std::max(1, editor_.line_count());
            int targetLine = 1;
            if (wParam != 0) {
                targetLine = std::clamp(static_cast<int>(lParam), 1, lineCount);
            } else {
                const auto scaled = static_cast<long long>(std::clamp<LPARAM>(lParam, 0, 1'000'000));
                targetLine = 1 + static_cast<int>((scaled * (lineCount - 1) + 500'000) / 1'000'000);
            }
            locatingFromPreview_ = true;
            editor_.go_to_line(targetLine);
            editor_.focus();
            locatingFromPreview_ = false;
            update_status();
        }
        return 0;
    case kMessagePreviewFindResult:
        previewFindStatus_ = lParam > 0
            ? i18n::text(L"Preview match") + L" " + std::to_wstring(wParam) + L"/" + std::to_wstring(lParam)
            : i18n::text(L"No preview match");
        update_status();
        return 0;
    case kMessagePreviewReady:
        if (previewRenderWorker_) {
            if (auto result = previewRenderWorker_->take_completed()) {
                previewRendering_ = false;
                preview_.set_content(std::move(result->html), result->folder, result->scrollFraction);
                if (mode_ == ViewMode::Split) schedule_preview_sync(true);
                update_status();
            }
        }
        return 0;
    case kMessageReloadConfiguration:
        reload_configuration(false);
        return 0;
    case kMessageRestoreEditorView:
        restore_pending_editor_view_state();
        return 0;
    case kMessageEditorMenu: {
        POINT point{};
        GetCursorPos(&point);
        show_editor_menu(point, true);
        return 0;
    }
    case kMessageZoom:
        if (static_cast<INT_PTR>(wParam) > 0) editor_.zoom_in();
        else if (static_cast<INT_PTR>(wParam) < 0) editor_.zoom_out();
        else editor_.reset_zoom();
        update_status();
        return 0;
    case kMessageToggleWrap:
        editor_.toggle_wrap();
        update_status();
        return 0;
    case kMessageSetEol:
        set_eol_mode(static_cast<int>(wParam));
        return 0;
    case kMessageSetLanguage:
        if (wParam <= static_cast<WPARAM>(SyntaxLanguage::Lisp)) {
            set_language(static_cast<SyntaxLanguage>(wParam));
        }
        return 0;
    case kMessageMarkdownFormat:
        if (document_.is_markdown() && mode_ != ViewMode::Preview &&
            wParam <= static_cast<WPARAM>(MarkdownFormat::InlineCode)) {
            editor_.apply_markdown_format(static_cast<MarkdownFormat>(wParam));
            update_status();
        }
        return 0;
    case kMessageMarkdownSlash:
        if (document_.is_markdown() && mode_ != ViewMode::Preview && wParam < kSlashCommands.size()) {
            editor_.insert_markdown_command(kSlashCommands[static_cast<std::size_t>(wParam)].id,
                LOWORD(lParam) > 0 ? LOWORD(lParam) : 3,
                HIWORD(lParam) > 0 ? HIWORD(lParam) : 3);
            update_status();
        }
        return 0;
    case WM_SETFOCUS:
        editor_.focus();
        return 0;
    case WM_DESTROY:
        hide_slash_popup();
        remember_document_view_state();
        if (editor_.modified() && !discardingChanges_) write_recovery_snapshot();
        else remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
        KillTimer(window_, kPreviewTimer);
        KillTimer(window_, kPreviewSyncTimer);
        KillTimer(window_, kExternalChangeTimer);
        KillTimer(window_, kRecoveryTimer);
        KillTimer(window_, kConfigurationStatusTimer);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void AppWindow::layout() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int toolbarHeight = 38;
    const int statusHeight = 24;
    MoveWindow(toolbar_, 0, 0, clientWidth, toolbarHeight, TRUE);
    MoveWindow(status_, 8, std::max(toolbarHeight, clientHeight - statusHeight),
        std::max(0, clientWidth - 16), statusHeight, TRUE);
    const int statusWidth = std::max(0, clientWidth - 16);
    const int caretEnd = std::min(statusWidth, 145);
    const int languageEnd = std::min(statusWidth, caretEnd + 190);
    const int encodingEnd = std::min(statusWidth, languageEnd + 150);
    const int eolEnd = std::min(statusWidth, encodingEnd + 115);
    const std::array<int, 5> statusParts = {caretEnd, languageEnd, encodingEnd, eolEnd, -1};
    SendMessageW(status_, SB_SETPARTS, statusParts.size(), reinterpret_cast<LPARAM>(statusParts.data()));

    const std::array<int, 7> ids = {
        kIdEdit, kIdSplit, kIdPreview, kIdSave, kIdTheme, kIdLanguage, kIdMore};
    HDC toolbarDevice = GetDC(toolbar_);
    HFONT oldFont = nullptr;
    if (toolbarDevice) {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(toolbar_, WM_GETFONT, 0, 0));
        if (!font) font = uiFont_ ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        oldFont = static_cast<HFONT>(SelectObject(toolbarDevice, font));
    }
    const auto buttonWidth = [&](HWND button) {
        wchar_t label[256]{};
        GetWindowTextW(button, label, static_cast<int>(std::size(label)));
        SIZE size{};
        if (!toolbarDevice || !GetTextExtentPoint32W(toolbarDevice, label,
                static_cast<int>(wcslen(label)), &size)) {
            size.cx = 34;
        }
        return std::max(52, static_cast<int>(size.cx) + 24);
    };

    int x = 6;
    for (int id : ids) {
        HWND button = GetDlgItem(toolbar_, id);
        const int width = buttonWidth(button);
        MoveWindow(button, x, 5, width, 28, TRUE);
        x += width + 4;
    }
    const HWND findButton = GetDlgItem(toolbar_, kIdFindNext);
    const HWND replaceButton = GetDlgItem(toolbar_, kIdReplace);
    const int findButtonWidth = buttonWidth(findButton);
    const int replaceButtonWidth = buttonWidth(replaceButton);
    if (toolbarDevice) {
        if (oldFont) SelectObject(toolbarDevice, oldFont);
        ReleaseDC(toolbar_, toolbarDevice);
    }

    const int desiredFindWidth = std::clamp(clientWidth / 5, 120, 240);
    const int findActionsWidth = 5 + findButtonWidth + 4 + replaceButtonWidth;
    const int leftLimit = x + 8;
    const int availableFindWidth = clientWidth - leftLimit - findActionsWidth - 6;
    const int findWidth = std::max(60, std::min(desiredFindWidth, availableFindWidth));
    const int findX = std::max(leftLimit, clientWidth - findWidth - findActionsWidth - 6);
    MoveWindow(findBox_, findX, 6, findWidth, 220, TRUE);
    const int findButtonX = findX + findWidth + 5;
    MoveWindow(findButton, findButtonX, 5, findButtonWidth, 28, TRUE);
    MoveWindow(replaceButton, findButtonX + findButtonWidth + 4, 5,
        replaceButtonWidth, 28, TRUE);

    RECT content{0, toolbarHeight, clientWidth, std::max(toolbarHeight, clientHeight - statusHeight)};
    if (mode_ == ViewMode::Split && document_.supports_preview()) {
        const int divider = 8;
        const int contentWidth = std::max(0, static_cast<int>(content.right - content.left));
        const int middle = content.left + static_cast<int>(std::lround(contentWidth * splitRatio_));
        RECT editorBounds{content.left, content.top, middle - divider / 2, content.bottom};
        RECT previewBounds{middle + divider / 2, content.top, content.right, content.bottom};
        editor_.resize(editorBounds);
        preview_.resize(previewBounds);
        SetWindowPos(splitDivider_, HWND_TOP, middle - divider / 2, content.top, divider,
            std::max(0, static_cast<int>(content.bottom - content.top)),
            SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        editor_.resize(content);
        preview_.resize(content);
        ShowWindow(splitDivider_, SW_HIDE);
    }
}

void AppWindow::restore_pending_editor_view_state() {
    if (!pendingEditorViewState_ || !editor_.handle()) return;
    const EditorViewState state = *pendingEditorViewState_;
    pendingEditorViewState_.reset();
    const bool wasRestoring = restoringDocumentState_;
    restoringDocumentState_ = true;
    editor_.restore_view_state(state);
    restoringDocumentState_ = wasRestoring;
    update_status();
}

void AppWindow::set_mode(ViewMode mode) {
    if (!document_.supports_preview() && mode != ViewMode::Edit) return;
    hide_slash_popup();
    mode_ = mode;
    if (mode == ViewMode::Edit) previewFindStatus_.clear();
    preview_.set_source_navigation_enabled(mode == ViewMode::Split);
    CheckRadioButton(toolbar_, kIdEdit, kIdPreview,
        mode == ViewMode::Edit ? kIdEdit : (mode == ViewMode::Split ? kIdSplit : kIdPreview));
    const bool editing = mode != ViewMode::Preview;
    editor_.show(editing);
    preview_.show(document_.supports_preview() && mode != ViewMode::Edit);
    EnableWindow(GetDlgItem(toolbar_, kIdSplit), document_.supports_preview());
    EnableWindow(GetDlgItem(toolbar_, kIdPreview), document_.supports_preview());
    EnableWindow(GetDlgItem(toolbar_, kIdSave), editing);
    EnableWindow(GetDlgItem(toolbar_, kIdMore), editing);
    EnableWindow(findBox_, editing || document_.supports_preview());
    EnableWindow(GetDlgItem(toolbar_, kIdFindNext), editing || document_.supports_preview());
    EnableWindow(GetDlgItem(toolbar_, kIdReplace), editing || document_.supports_preview());
    layout();
    if (editing) editor_.focus();
    if (mode_ == ViewMode::Split) schedule_preview_sync(true);
}

void AppWindow::refresh_preview(std::optional<double> initialScrollFraction) {
    constexpr std::size_t backgroundThreshold = 256 * 1024;
    constexpr std::size_t maximumPreviewSize = 8 * 1024 * 1024;
    if (!document_.supports_preview()) {
        if (previewRenderWorker_) previewRenderWorker_->cancel();
        previewRendering_ = false;
        return;
    }
    const std::string source = editor_.text();
    if (source.size() > maximumPreviewSize) {
        if (previewRenderWorker_) previewRenderWorker_->cancel();
        previewRendering_ = false;
        preview_.set_content(L"<!doctype html><meta charset=utf-8><style>body{font:16px Segoe UI;padding:32px}</style><p>" +
            i18n::text(L"Live preview is paused for this large document. Editing and saving remain available.") + L"</p>",
            document_.path().parent_path(), initialScrollFraction);
    } else if (source.size() >= backgroundThreshold && previewRenderWorker_) {
        PreviewRenderWorker::Request request;
        request.html = document_.is_html();
        request.dark = dark_;
        request.source = source;
        request.title = wide_to_utf8(file_title(document_.path()));
        request.folder = document_.path().parent_path();
        request.scrollFraction = initialScrollFraction;
        previewRendering_ = true;
        previewRenderWorker_->submit(std::move(request));
        update_status();
        return;
    } else if (document_.is_html()) {
        if (previewRenderWorker_) previewRenderWorker_->cancel();
        previewRendering_ = false;
        preview_.set_content(render_html_preview(source, "https://editmdview.local/"),
            document_.path().parent_path(), initialScrollFraction);
    } else {
        if (previewRenderWorker_) previewRenderWorker_->cancel();
        previewRendering_ = false;
        MarkdownRenderOptions options;
        options.dark = dark_;
        options.baseHref = "https://editmdview.local/";
        options.title = wide_to_utf8(file_title(document_.path()));
        preview_.set_content(render_markdown_html(source, options), document_.path().parent_path(),
            initialScrollFraction);
    }
    if (mode_ == ViewMode::Split) schedule_preview_sync(true);
}

void AppWindow::schedule_preview_sync(bool useCaretLine) {
    if (mode_ != ViewMode::Split || !document_.supports_preview()) return;
    if (useCaretLine) {
        pendingSyncLine_ = std::max(0, editor_.current_line() - 1);
    } else {
        pendingSyncLine_ = editor_.first_visible_document_line() + editor_.visible_line_count() / 3;
    }
    pendingSyncLine_ = std::min(pendingSyncLine_, editor_.line_count() - 1);
    KillTimer(window_, kPreviewSyncTimer);
    SetTimer(window_, kPreviewSyncTimer, kPreviewSyncDelayMs, nullptr);
}

void AppWindow::sync_preview_to_editor() {
    if (mode_ != ViewMode::Split || !document_.supports_preview()) return;
    const int lines = editor_.line_count();
    const double fraction = lines <= 1 ? 0.0 :
        static_cast<double>(std::clamp(pendingSyncLine_, 0, lines - 1)) /
            static_cast<double>(lines - 1);
    preview_.scroll_to_fraction(fraction);
}

void AppWindow::check_external_change() {
    if (closing_ || document_.path().empty()) return;
    if (!document_.changed_on_disk()) {
        if (externalChangeNotified_) {
            externalChangeNotified_ = false;
            update_status();
        }
        return;
    }
    if (externalChangeNotified_) return;
    externalChangeNotified_ = true;
    if (editor_.modified()) {
        update_status();
        MessageBoxW(window_,
            i18n::text(L"The file was modified by another program.\n\nUnsaved editor content was not overwritten. Resolve the external version before saving.").c_str(),
            i18n::text(L"External Change Detected").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    const std::filesystem::path path = document_.path();
    if (load_file(path)) {
        externalChangeNotified_ = false;
        update_status();
    }
}

void AppWindow::check_configuration_change() {
    if (closing_ || !editor_.handle()) return;
    const std::uint64_t current = editor_.configuration_signature();
    if (current != configurationSignature_) reload_configuration(true);
}

void AppWindow::reload_configuration(bool automatic, bool announce) {
    if (!editor_.handle() || document_.path().empty()) return;
    i18n::initialize(module_path(instance_));
    const EditorViewState view = editor_.view_state();
    auto properties = SciteProperties::load_for_document(
        module_path(instance_), document_.path(), dark_);
    configurationSignature_ = properties.configuration_signature();
    editor_.set_properties(std::move(properties));
    editor_.reload_configuration(document_.path(), dark_);
    editor_.restore_view_state(view);
    if (announce) {
        configurationStatus_ = automatic ? i18n::text(L"Configuration reloaded automatically") :
            i18n::text(L"Configuration reloaded");
        KillTimer(window_, kConfigurationStatusTimer);
        SetTimer(window_, kConfigurationStatusTimer, kConfigurationStatusDelayMs, nullptr);
    }
    update_status();
}

void AppWindow::show_status_menu(int part) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    if (part == kStatusEol) {
        const std::wstring current = editor_.eol_name();
        AppendMenuW(menu, MF_STRING | (current == L"CRLF" ? MF_CHECKED : 0), kMenuEolCrLf,
            L"Windows (CRLF)");
        AppendMenuW(menu, MF_STRING | (current == L"LF" ? MF_CHECKED : 0), kMenuEolLf,
            L"Unix / Linux (LF)");
        AppendMenuW(menu, MF_STRING | (current == L"CR" ? MF_CHECKED : 0), kMenuEolCr,
            i18n::text(L"Classic Mac (CR)").c_str());
    } else if (part == kStatusLanguage) {
        for (std::size_t index = 0; index < kLanguages.size(); ++index) {
            if (index == 2 || index == 12) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            const auto [language, name] = kLanguages[index];
            const UINT flags = MF_STRING | (language == editor_.language() ? MF_CHECKED : 0);
            const std::wstring localizedName = i18n::text(name);
            AppendMenuW(menu, flags, kMenuLanguageBase + static_cast<UINT>(index), localizedName.c_str());
        }
    } else {
        DestroyMenu(menu);
        return;
    }

    RECT partBounds{};
    SendMessageW(status_, SB_GETRECT, part, reinterpret_cast<LPARAM>(&partBounds));
    POINT menuPoint{partBounds.left, partBounds.bottom};
    ClientToScreen(status_, &menuPoint);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        menuPoint.x, menuPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);

    if (command == kMenuEolCrLf) set_eol_mode(SC_EOL_CRLF);
    else if (command == kMenuEolLf) set_eol_mode(SC_EOL_LF);
    else if (command == kMenuEolCr) set_eol_mode(SC_EOL_CR);
    else if (command >= kMenuLanguageBase &&
        command < kMenuLanguageBase + static_cast<int>(kLanguages.size())) {
        set_language(kLanguages[static_cast<std::size_t>(command - kMenuLanguageBase)].first);
    }
}

void AppWindow::set_eol_mode(int scintillaEolMode) {
    editor_.convert_eol(scintillaEolMode);
    update_status();
    if (mode_ != ViewMode::Preview) editor_.focus();
}

void AppWindow::set_language(SyntaxLanguage language) {
    editor_.set_language(language);
    update_status();
    if (mode_ != ViewMode::Preview) editor_.focus();
}

void AppWindow::update_status() {
    if (!status_) return;
    const std::wstring caret = i18n::text(L"Line") + L" " +
        std::to_wstring(editor_.current_line()) + L", " + i18n::text(L"Column") + L" " +
        std::to_wstring(editor_.current_column());
    const std::wstring language = i18n::text(L"Language:") + L" " + editor_.language_name() + L"  ▾";
    const std::wstring encoding = i18n::text(L"Encoding:") + L" " + document_.encoding_name();
    const std::wstring eol = i18n::text(L"EOL:") + L" " + editor_.eol_name() + L"  ▾";
    std::wstring state = editor_.wrap_enabled() ? i18n::text(L"Word wrap") : i18n::text(L"No wrap");
    if (editor_.zoom() != 0) {
        state += L"    " + i18n::text(L"Zoom") + L" " +
            std::wstring(editor_.zoom() > 0 ? L"+" : L"") + std::to_wstring(editor_.zoom());
    }
    if (editor_.modified()) state += L"    ● " + i18n::text(L"Modified");
    if (recoveryWriteFailed_) state += L"    ⚠ " + i18n::text(L"Unable to save recovery draft");
    if (externalChangeNotified_) state += L"    ⚠ " + i18n::text(L"File changed externally");
    if (previewRendering_) state += L"    " + i18n::text(L"Rendering preview…");
    if (!configurationStatus_.empty()) state += L"    " + configurationStatus_;
    if (!previewFindStatus_.empty() && mode_ != ViewMode::Edit) state += L"    " + previewFindStatus_;
    SendMessageW(status_, SB_SETTEXTW, kStatusCaret, reinterpret_cast<LPARAM>(caret.c_str()));
    SendMessageW(status_, SB_SETTEXTW, kStatusLanguage | SBT_POPOUT,
        reinterpret_cast<LPARAM>(language.c_str()));
    SendMessageW(status_, SB_SETTEXTW, kStatusEncoding, reinterpret_cast<LPARAM>(encoding.c_str()));
    SendMessageW(status_, SB_SETTEXTW, kStatusEol | SBT_POPOUT, reinterpret_cast<LPARAM>(eol.c_str()));
    SendMessageW(status_, SB_SETTEXTW, kStatusState, reinterpret_cast<LPARAM>(state.c_str()));
    SendMessageW(status_, SB_SETTIPTEXTW, kStatusLanguage,
        reinterpret_cast<LPARAM>(i18n::text(L"Click to change the syntax highlighting language").c_str()));
    SendMessageW(status_, SB_SETTIPTEXTW, kStatusEol,
        reinterpret_cast<LPARAM>(i18n::text(L"Click to convert line endings for the whole document").c_str()));
    SendMessageW(status_, SB_SETTIPTEXTW, kStatusState,
        reinterpret_cast<LPARAM>(i18n::text(L"Click the wrap indicator to toggle word wrap").c_str()));
}

void AppWindow::save() {
    if (!editor_.modified()) return;
    std::wstring error;
    const std::string content = editor_.text_for_save();
    if (!document_.save(content, error,
            [this](const std::filesystem::path& stagedFile,
                const std::filesystem::path& targetFile, std::wstring& saveError) {
                return run_elevated_save_helper(window_, instance_, stagedFile, targetFile, saveError);
            })) {
        MessageBoxW(window_, error.c_str(), i18n::text(L"Save Failed").c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    editor_.mark_saved();
    KillTimer(window_, kRecoveryTimer);
    recoverySnapshotContent_.clear();
    recoveryWriteFailed_ = false;
    remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
    externalChangeNotified_ = false;
    update_status();
}

void AppWindow::save_as() {
    std::filesystem::path targetPath;
    std::wstring error;
    if (!prompt_for_save_path(window_, document_.path(), targetPath, error)) {
        if (!error.empty()) MessageBoxW(window_, error.c_str(), i18n::text(L"Save As Failed").c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    const std::filesystem::path originalPath = document_.path();
    const EditorViewState view = editor_.view_state();
    const double previewScroll = preview_.scroll_fraction();
    remember_document_view_state();
    const std::string content = editor_.text_for_save();
    if (!document_.save_as(targetPath, content, error,
            [this](const std::filesystem::path& stagedFile,
                const std::filesystem::path& targetFile, std::wstring& saveError) {
                return run_elevated_save_helper(window_, instance_, stagedFile, targetFile, saveError);
            })) {
        MessageBoxW(window_, error.c_str(), i18n::text(L"Save As Failed").c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    editor_.mark_saved();
    KillTimer(window_, kRecoveryTimer);
    recoverySnapshotContent_.clear();
    recoveryWriteFailed_ = false;
    remove_recovery_snapshot(runtimeDataDirectory_, originalPath);
    remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
    externalChangeNotified_ = false;

    auto properties = SciteProperties::load_for_document(
        module_path(instance_), document_.path(), dark_);
    configurationSignature_ = properties.configuration_signature();
    editor_.set_properties(std::move(properties));
    editor_.reload_configuration(document_.path(), dark_);

    std::wstring previewError;
    if (document_.supports_preview() && !preview_.handle()) {
        preview_.create(window_, instance_, previewError);
        preview_.set_find_shortcuts(window_, kMessageFocusFind, kMessageFindNext,
            kMessagePreviewFindResult, kMessageToggleMode, kMessageReloadConfiguration,
            kMessageSave, kMessageSaveAs);
        preview_.set_source_navigation(window_, kMessagePreviewLocate);
    }
    const ViewMode nextMode = document_.supports_preview() ? mode_ : ViewMode::Edit;
    set_mode(nextMode);
    editor_.restore_view_state(view);
    if (document_.supports_preview()) refresh_preview(previewScroll);

    configurationStatus_ = i18n::text(L"Saved as") + L" \"" +
        file_title(document_.path()) + L"\"";
    KillTimer(window_, kConfigurationStatusTimer);
    SetTimer(window_, kConfigurationStatusTimer, kConfigurationStatusDelayMs, nullptr);
    remember_document_view_state();
    update_status();
}

bool AppWindow::confirm_discard_or_save() {
    if (!editor_.modified()) return true;
    const std::wstring prompt = L"\"" + file_title(document_.path()) + L"\" " +
        i18n::text(L"has unsaved changes.\n\nSave them?");
    const int answer = MessageBoxW(window_, prompt.c_str(), L"EditMdView", MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) {
        save();
        return !editor_.modified();
    }
    return true;
}

void AppWindow::toggle_theme() {
    dark_ = !dark_;
    auto properties = SciteProperties::load_for_document(
        module_path(instance_), document_.path(), dark_);
    configurationSignature_ = properties.configuration_signature();
    editor_.set_properties(std::move(properties));
    editor_.set_dark(dark_);
    SetWindowTextW(GetDlgItem(toolbar_, kIdTheme),
        dark_ ? i18n::text(L"Light").c_str() : i18n::text(L"Dark").c_str());
    refresh_preview();
}

void AppWindow::show_language_menu(POINT screenPoint) {
    const auto module = module_path(instance_);
    const auto languages = i18n::available_languages(module);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const std::wstring selected = lower_wide(i18n::selected_language());
    for (std::size_t index = 0; index < languages.size(); ++index) {
        const std::wstring code = languages[index];
        std::wstring label = code;
        if (lower_wide(code) == L"auto") label = i18n::text(L"Automatic (Windows)");
        else if (lower_wide(code) == L"zh-cn") label = i18n::text(L"Simplified Chinese");
        else if (lower_wide(code) == L"en-us") label = L"English";
        const UINT flags = MF_STRING |
            (lower_wide(code) == selected ? static_cast<UINT>(MF_CHECKED) : 0U);
        AppendMenuW(menu, flags, kMenuUiLanguageBase + static_cast<UINT>(index), label.c_str());
    }
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPoint.x, screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command < kMenuUiLanguageBase ||
        command >= kMenuUiLanguageBase + static_cast<int>(languages.size())) return;
    std::wstring error;
    if (!i18n::select_language(module,
            languages[static_cast<std::size_t>(command - kMenuUiLanguageBase)], error)) {
        MessageBoxW(window_, error.c_str(), L"EditMdView", MB_OK | MB_ICONERROR);
        return;
    }
    apply_ui_language();
}

void AppWindow::apply_ui_language() {
    SetWindowTextW(GetDlgItem(toolbar_, kIdEdit), i18n::text(L"Edit").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdSplit), i18n::text(L"Split").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdPreview), i18n::text(L"Preview").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdSave), i18n::text(L"Save").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdTheme),
        dark_ ? i18n::text(L"Light").c_str() : i18n::text(L"Dark").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdLanguage), i18n::text(L"Language").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdMore), i18n::text(L"More").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdFindNext), i18n::text(L"Find").c_str());
    SetWindowTextW(GetDlgItem(toolbar_, kIdReplace), i18n::text(L"Replace").c_str());
    if (replaceDialog_ && IsWindow(replaceDialog_)) DestroyWindow(replaceDialog_);
    hide_slash_popup();
    reload_configuration(false, false);
    if (document_.supports_preview()) refresh_preview(preview_.scroll_fraction());
    layout();
    update_status();
}

bool AppWindow::prefill_find_from_selection() {
    const std::string selected = editor_.selected_text();
    if (selected.empty() || selected.size() > 4096 ||
        selected.find_first_of("\r\n") != std::string::npos) return false;
    const std::wstring query = utf8_to_wide(selected);
    if (query.empty()) return false;
    SetWindowTextW(findBox_, query.c_str());
    if (replaceFindCombo_) SetWindowTextW(replaceFindCombo_, query.c_str());
    return true;
}

void AppWindow::perform_find(bool backwards, bool fromStart) {
    const std::wstring query = window_text(findBox_);
    if (query.empty()) {
        SetFocus(findEdit_ ? findEdit_ : findBox_);
        return;
    }
    remember_search_history(query);
    const bool queryChanged = query != lastPreviewFind_;
    if (mode_ != ViewMode::Edit) {
        lastPreviewFind_ = query;
        preview_.find(query, backwards, fromStart || queryChanged);
        if (mode_ == ViewMode::Preview) return;
    }
    if (!editor_.find(wide_to_utf8(query), false, false, backwards, fromStart)) {
        MessageBeep(MB_ICONINFORMATION);
    }
}

void AppWindow::show_editor_menu(POINT screenPoint, bool contextMenu) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    if (contextMenu) {
        // Keep the editor context menu focused on frequent editing actions.
        AppendMenuW(menu, MF_STRING, kMenuUndo, i18n::text(L"Undo\tCtrl+Z").c_str());
        AppendMenuW(menu, MF_STRING, kMenuRedo, i18n::text(L"Redo\tCtrl+Y").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuCut, i18n::text(L"Cut\tCtrl+X").c_str());
        AppendMenuW(menu, MF_STRING, kMenuCopy, i18n::text(L"Copy\tCtrl+C").c_str());
        AppendMenuW(menu, MF_STRING, kMenuPaste, i18n::text(L"Paste\tCtrl+V").c_str());
        AppendMenuW(menu, MF_STRING, kMenuDelete, i18n::text(L"Delete").c_str());
        AppendMenuW(menu, MF_STRING, kMenuSelectAll, i18n::text(L"Select All\tCtrl+A").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuReplace, i18n::text(L"Replace…\tCtrl+H").c_str());
    } else {
        // Less frequent document and view commands live under More.
        AppendMenuW(menu, MF_STRING, kMenuSaveAs, i18n::text(L"Save As…\tCtrl+Shift+S").c_str());
        AppendMenuW(menu, MF_STRING, kMenuGoToLine, i18n::text(L"Go to Line…\tCtrl+G").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (editor_.wrap_enabled() ? MF_CHECKED : 0),
            kMenuWrap, i18n::text(L"Word Wrap\tAlt+Z").c_str());
        AppendMenuW(menu, MF_STRING, kMenuZoomIn, i18n::text(L"Zoom In\tCtrl++").c_str());
        AppendMenuW(menu, MF_STRING, kMenuZoomOut, i18n::text(L"Zoom Out\tCtrl+-").c_str());
        AppendMenuW(menu, MF_STRING, kMenuZoomReset, i18n::text(L"Reset Zoom\tCtrl+0").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuReloadConfiguration,
            i18n::text(L"Reload Configuration\tCtrl+Shift+R").c_str());
    }

    if (contextMenu) {
        EnableMenuItem(menu, kMenuUndo,
            MF_BYCOMMAND | (editor_.can_undo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, kMenuRedo,
            MF_BYCOMMAND | (editor_.can_redo() ? MF_ENABLED : MF_GRAYED));
        const bool selection = editor_.has_selection();
        EnableMenuItem(menu, kMenuCut, MF_BYCOMMAND | (selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, kMenuCopy, MF_BYCOMMAND | (selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, kMenuDelete, MF_BYCOMMAND | (selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, kMenuPaste,
            MF_BYCOMMAND | (editor_.can_paste() ? MF_ENABLED : MF_GRAYED));
    }

    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPoint.x, screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
    case kMenuSaveAs: save_as(); break;
    case kMenuUndo: editor_.undo(); break;
    case kMenuRedo: editor_.redo(); break;
    case kMenuCut: editor_.cut(); break;
    case kMenuCopy: editor_.copy(); break;
    case kMenuPaste:
        if (!editor_.paste_markdown_smart()) editor_.paste();
        break;
    case kMenuDelete: editor_.delete_selection(); break;
    case kMenuSelectAll: editor_.select_all(); break;
    case kMenuReplace: show_replace_dialog(); break;
    case kMenuGoToLine: show_go_to_line(); break;
    case kMenuWrap: editor_.toggle_wrap(); break;
    case kMenuZoomIn: editor_.zoom_in(); break;
    case kMenuZoomOut: editor_.zoom_out(); break;
    case kMenuZoomReset: editor_.reset_zoom(); break;
    case kMenuReloadConfiguration: reload_configuration(false); break;
    default: break;
    }
    if (command != 0 && command != kMenuReplace) {
        editor_.focus();
        update_status();
    }
}

void AppWindow::show_go_to_line() {
    const int line = prompt_for_line(window_, instance_, uiFont_, editor_.current_line());
    if (line > 0) editor_.go_to_line(line);
    editor_.focus();
    update_status();
}

void AppWindow::show_replace_dialog() {
    const bool selectionPrefilled = prefill_find_from_selection();
    if (mode_ == ViewMode::Preview) set_mode(ViewMode::Edit);
    if (replaceDialog_ && IsWindow(replaceDialog_)) {
        if (selectionPrefilled) SetWindowTextW(replaceFindCombo_, window_text(findBox_).c_str());
        ShowWindow(replaceDialog_, SW_RESTORE);
        SetForegroundWindow(replaceDialog_);
        return;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = replace_window_proc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Child labels, check boxes and push buttons use the standard dialog
    // colour. Match the parent to it so they do not appear as grey blocks.
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kReplaceWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBeep(MB_ICONERROR);
        return;
    }

    HWND owner = GetAncestor(window_, GA_ROOT);
    RECT ownerBounds{};
    GetWindowRect(owner, &ownerBounds);
    RECT bounds{0, 0, 530, 266};
    AdjustWindowRectEx(&bounds, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
        WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = ownerBounds.left + ((ownerBounds.right - ownerBounds.left) - width) / 2;
    const int y = ownerBounds.top + ((ownerBounds.bottom - ownerBounds.top) - height) / 2;
    replaceDialog_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kReplaceWindowClass,
        i18n::text(L"Find and Replace").c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
        owner, nullptr, instance_, this);
    if (!replaceDialog_) {
        MessageBeep(MB_ICONERROR);
        return;
    }

    const HFONT font = uiFont_ ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto make_control = [&](DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style,
                            int left, int top, int controlWidth, int controlHeight, int id) {
        HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
            left, top, controlWidth, controlHeight, replaceDialog_,
            id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };

    make_control(0, L"STATIC", i18n::text(L"Find:").c_str(), SS_CENTERIMAGE, 18, 18, 76, 26, 0);
    replaceFindCombo_ = make_control(0, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWN | CBS_AUTOHSCROLL, 96, 18, 406, 220, kReplaceFind);
    make_control(0, L"STATIC", i18n::text(L"Replace with:").c_str(), SS_CENTERIMAGE, 18, 54, 76, 26, 0);
    replaceWithCombo_ = make_control(0, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWN | CBS_AUTOHSCROLL, 96, 54, 406, 220, kReplaceWith);
    make_control(0, L"BUTTON", i18n::text(L"Match case").c_str(), WS_TABSTOP | BS_AUTOCHECKBOX,
        18, 94, 110, 24, kReplaceMatchCase);
    make_control(0, L"BUTTON", i18n::text(L"Whole word").c_str(), WS_TABSTOP | BS_AUTOCHECKBOX,
        140, 94, 96, 24, kReplaceWholeWord);
    make_control(0, L"BUTTON", i18n::text(L"Search up").c_str(), WS_TABSTOP | BS_AUTOCHECKBOX,
        248, 94, 96, 24, kReplaceDirectionUp);
    make_control(0, L"BUTTON", i18n::text(L"Find Next").c_str(), WS_TABSTOP | BS_DEFPUSHBUTTON,
        18, 132, 108, 30, kReplaceFindNext);
    make_control(0, L"BUTTON", i18n::text(L"Replace").c_str(), WS_TABSTOP | BS_PUSHBUTTON,
        136, 132, 92, 30, kReplaceOne);
    make_control(0, L"BUTTON", i18n::text(L"Replace All").c_str(), WS_TABSTOP | BS_PUSHBUTTON,
        238, 132, 102, 30, kReplaceAll);
    make_control(0, L"BUTTON", i18n::text(L"Close").c_str(), WS_TABSTOP | BS_PUSHBUTTON,
        410, 132, 92, 30, kReplaceClose);
    replaceResult_ = make_control(0, L"STATIC", L"", SS_CENTERIMAGE,
        18, 174, 484, 28, kReplaceResult);

    const std::wstring currentFind = window_text(findBox_);
    populate_history_combo(replaceFindCombo_, findHistory_, currentFind);
    populate_history_combo(replaceWithCombo_, replaceHistory_);
    std::array<HWND, 9> controls = {
        FindWindowExW(replaceFindCombo_, nullptr, L"Edit", nullptr),
        FindWindowExW(replaceWithCombo_, nullptr, L"Edit", nullptr),
        GetDlgItem(replaceDialog_, kReplaceMatchCase), GetDlgItem(replaceDialog_, kReplaceWholeWord),
        GetDlgItem(replaceDialog_, kReplaceDirectionUp), GetDlgItem(replaceDialog_, kReplaceFindNext),
        GetDlgItem(replaceDialog_, kReplaceOne), GetDlgItem(replaceDialog_, kReplaceAll),
        GetDlgItem(replaceDialog_, kReplaceClose)};
    for (HWND control : controls) {
        if (control) SetWindowSubclass(control, replace_control_subclass_proc, 4,
            reinterpret_cast<DWORD_PTR>(this));
    }

    ShowWindow(replaceDialog_, SW_SHOW);
    SetForegroundWindow(replaceDialog_);
    if (controls[0]) {
        SetFocus(controls[0]);
        SendMessageW(controls[0], EM_SETSEL, 0, -1);
    }
}

void AppWindow::perform_replace_action(int command) {
    const std::wstring needle = window_text(replaceFindCombo_);
    const std::wstring replacement = window_text(replaceWithCombo_);
    if (needle.empty()) {
        MessageBeep(MB_ICONWARNING);
        if (replaceResult_) SetWindowTextW(replaceResult_, i18n::text(L"Enter text to find.").c_str());
        return;
    }
    SetWindowTextW(findBox_, needle.c_str());
    remember_search_history(needle,
        command == kReplaceFindNext ? std::nullopt : std::optional<std::wstring_view>(replacement));
    const std::string utf8Needle = wide_to_utf8(needle);
    const std::string utf8Replacement = wide_to_utf8(replacement);
    const bool matchCase = SendMessageW(GetDlgItem(replaceDialog_, kReplaceMatchCase),
        BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool wholeWord = SendMessageW(GetDlgItem(replaceDialog_, kReplaceWholeWord),
        BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool backwards = SendMessageW(GetDlgItem(replaceDialog_, kReplaceDirectionUp),
        BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (command == kReplaceFindNext) {
        if (!editor_.find(utf8Needle, matchCase, wholeWord, backwards, false)) {
            MessageBeep(MB_ICONINFORMATION);
            if (replaceResult_) SetWindowTextW(replaceResult_, i18n::text(L"No matches found.").c_str());
        } else if (replaceResult_) {
            SetWindowTextW(replaceResult_, i18n::text(L"Moved to the next match.").c_str());
        }
    } else if (command == kReplaceOne) {
        const bool replaced = editor_.replace_selection_if_match(
            utf8Needle, utf8Replacement, matchCase, wholeWord);
        const bool found = editor_.find(utf8Needle, matchCase, wholeWord, backwards, false);
        if (!replaced && !found) MessageBeep(MB_ICONINFORMATION);
        const std::wstring result = replaced
            ? i18n::text(L"Replaced the current match and continued searching.")
            : (found ? i18n::text(L"Match selected; click Replace again to replace it.")
                     : i18n::text(L"No matches found."));
        if (replaceResult_) SetWindowTextW(replaceResult_, result.c_str());
    } else if (command == kReplaceAll) {
        const int count = editor_.replace_all(utf8Needle, utf8Replacement, matchCase, wholeWord);
        const std::wstring message = i18n::text(L"Replaced") + L" " +
            std::to_wstring(count) + L" " + i18n::text(L"occurrence(s).");
        if (replaceResult_) SetWindowTextW(replaceResult_, message.c_str());
    }
    update_status();
}

void AppWindow::update_slash_popup() {
    if (!document_.is_markdown() || mode_ == ViewMode::Preview || slashTableMode_) {
        if (!slashTableMode_) hide_slash_popup();
        return;
    }
    std::intptr_t slashStart = 0;
    std::intptr_t slashEnd = 0;
    std::string query;
    if (!editor_.markdown_slash_context(slashStart, slashEnd, query)) {
        hide_slash_popup();
        return;
    }

    const int previousCommand = slashPopup_ && slashSelected_ >= 0 &&
        slashSelected_ < static_cast<int>(slashVisibleCommands_.size())
        ? slashVisibleCommands_[static_cast<std::size_t>(slashSelected_)] : -1;
    const std::wstring filter = lower_wide(utf8_to_wide(query));
    slashVisibleCommands_.clear();
    for (std::size_t index = 0; index < kSlashCommands.size(); ++index) {
        const SlashCommand& command = kSlashCommands[index];
        std::wstring searchable = utf8_to_wide(command.id);
        searchable += L" ";
        searchable += command.label;
        searchable += L" ";
        searchable += i18n::text(command.label);
        searchable += L" ";
        searchable += command.description;
        searchable += L" ";
        searchable += i18n::text(command.description);
        searchable += L" ";
        searchable += command.aliases;
        searchable = lower_wide(std::move(searchable));
        if (filter.empty() || searchable.find(filter) != std::wstring::npos) {
            slashVisibleCommands_.push_back(static_cast<int>(index));
        }
    }
    if (slashVisibleCommands_.empty()) {
        hide_slash_popup();
        return;
    }
    slashSelected_ = 0;
    if (previousCommand >= 0) {
        const auto previous = std::find(slashVisibleCommands_.begin(), slashVisibleCommands_.end(),
            previousCommand);
        if (previous != slashVisibleCommands_.end()) {
            slashSelected_ = static_cast<int>(std::distance(slashVisibleCommands_.begin(), previous));
        }
    }
    slashScrollOffset_ = std::clamp(slashScrollOffset_, 0,
        std::max(0, static_cast<int>(slashVisibleCommands_.size()) - kSlashMaxVisible));
    if (slashSelected_ < slashScrollOffset_) slashScrollOffset_ = slashSelected_;
    if (slashSelected_ >= slashScrollOffset_ + kSlashMaxVisible) {
        slashScrollOffset_ = slashSelected_ - kSlashMaxVisible + 1;
    }

    const int visibleCount = std::min(kSlashMaxVisible,
        static_cast<int>(slashVisibleCommands_.size()));
    const int popupHeight = visibleCount * kSlashItemHeight;
    POINT point = editor_.caret_screen_point();
    ScreenToClient(window_, &point);
    RECT client{};
    GetClientRect(window_, &client);
    const int clientRight = static_cast<int>(client.right);
    const int clientBottom = static_cast<int>(client.bottom);
    int x = std::clamp(static_cast<int>(point.x), 6,
        std::max(6, clientRight - kSlashPopupWidth - 6));
    int y = static_cast<int>(point.y) + 3;
    const int contentBottom = std::max(38, clientBottom - 26);
    if (y + popupHeight > contentBottom) {
        y = std::max(40, static_cast<int>(point.y) - popupHeight - 22);
    }

    if (!slashPopup_) {
        slashPopup_ = CreateWindowExW(0, kSlashWindowClass, L"",
            WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS, x, y, kSlashPopupWidth, popupHeight,
            window_, nullptr, instance_, this);
        if (!slashPopup_) return;
    }
    editor_.cancel_auto_completion();
    SetWindowPos(slashPopup_, HWND_TOP, x, y, kSlashPopupWidth, popupHeight,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(slashPopup_, nullptr, FALSE);
}

void AppWindow::hide_slash_popup(bool restoreEditorFocus) {
    if (slashPopup_ && IsWindow(slashPopup_)) DestroyWindow(slashPopup_);
    slashPopup_ = nullptr;
    slashRowsEdit_ = nullptr;
    slashColumnsEdit_ = nullptr;
    slashInsertButton_ = nullptr;
    slashVisibleCommands_.clear();
    slashSelected_ = 0;
    slashScrollOffset_ = 0;
    slashWheelRemainder_ = 0;
    slashTableMode_ = false;
    if (restoreEditorFocus && mode_ != ViewMode::Preview) editor_.focus();
}

void AppWindow::move_slash_selection(int delta) {
    if (!slashPopup_ || slashVisibleCommands_.empty()) return;
    slashSelected_ = std::clamp(slashSelected_ + delta, 0,
        static_cast<int>(slashVisibleCommands_.size()) - 1);
    if (slashSelected_ < slashScrollOffset_) slashScrollOffset_ = slashSelected_;
    if (slashSelected_ >= slashScrollOffset_ + kSlashMaxVisible) {
        slashScrollOffset_ = slashSelected_ - kSlashMaxVisible + 1;
    }
    InvalidateRect(slashPopup_, nullptr, FALSE);
}

void AppWindow::scroll_slash_popup(int wheelDelta) {
    if (!slashPopup_ || slashTableMode_ ||
        static_cast<int>(slashVisibleCommands_.size()) <= kSlashMaxVisible || wheelDelta == 0) return;
    slashWheelRemainder_ += wheelDelta;
    bool changed = false;
    const int maximumOffset = static_cast<int>(slashVisibleCommands_.size()) - kSlashMaxVisible;
    while (std::abs(slashWheelRemainder_) >= WHEEL_DELTA) {
        const int direction = slashWheelRemainder_ > 0 ? -1 : 1;
        slashWheelRemainder_ -= slashWheelRemainder_ > 0 ? WHEEL_DELTA : -WHEEL_DELTA;
        const int nextOffset = std::clamp(slashScrollOffset_ + direction, 0, maximumOffset);
        changed = changed || nextOffset != slashScrollOffset_;
        slashScrollOffset_ = nextOffset;
    }
    if (!changed) return;
    slashSelected_ = std::clamp(slashSelected_, slashScrollOffset_,
        std::min(static_cast<int>(slashVisibleCommands_.size()) - 1,
            slashScrollOffset_ + kSlashMaxVisible - 1));
    InvalidateRect(slashPopup_, nullptr, FALSE);
}

void AppWindow::execute_slash_selection() {
    if (!slashPopup_ || slashVisibleCommands_.empty() || slashSelected_ < 0 ||
        slashSelected_ >= static_cast<int>(slashVisibleCommands_.size())) return;
    execute_slash_command(slashVisibleCommands_[static_cast<std::size_t>(slashSelected_)]);
}

void AppWindow::execute_slash_command(int commandIndex) {
    if (commandIndex < 0 || commandIndex >= static_cast<int>(kSlashCommands.size())) return;
    if (std::string_view(kSlashCommands[static_cast<std::size_t>(commandIndex)].id) == "table") {
        show_table_form();
        return;
    }
    const bool inserted = editor_.insert_markdown_command(
        kSlashCommands[static_cast<std::size_t>(commandIndex)].id);
    hide_slash_popup(inserted);
    if (!inserted) MessageBeep(MB_ICONWARNING);
    update_status();
}

void AppWindow::show_table_form() {
    if (!slashPopup_ || slashTableMode_) return;
    slashTableMode_ = true;
    slashVisibleCommands_.clear();
    slashSelected_ = 0;
    slashScrollOffset_ = 0;
    slashWheelRemainder_ = 0;
    RECT popup{};
    GetWindowRect(slashPopup_, &popup);
    POINT position{popup.left, popup.top};
    ScreenToClient(window_, &position);
    SetWindowPos(slashPopup_, HWND_TOP, position.x, position.y, 286, 126, SWP_SHOWWINDOW);

    const HFONT font = uiFont_ ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto make_control = [&](const wchar_t* className, const wchar_t* text, DWORD style,
                            int x, int y, int width, int height, int id) {
        HWND control = CreateWindowExW(className == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
            className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, slashPopup_,
            id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };
    make_control(L"STATIC", i18n::text(L"Line").c_str(), SS_CENTERIMAGE, 18, 49, 24, 26, 0);
    slashRowsEdit_ = make_control(L"EDIT", L"3", WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        43, 49, 50, 26, kSlashRows);
    make_control(L"STATIC", i18n::text(L"Column").c_str(), SS_CENTERIMAGE, 106, 49, 24, 26, 0);
    slashColumnsEdit_ = make_control(L"EDIT", L"3", WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        131, 49, 50, 26, kSlashColumns);
    slashInsertButton_ = make_control(L"BUTTON", i18n::text(L"Insert").c_str(), WS_TABSTOP | BS_DEFPUSHBUTTON,
        194, 47, 72, 30, kSlashInsert);
    for (HWND control : {slashRowsEdit_, slashColumnsEdit_, slashInsertButton_}) {
        if (control) SetWindowSubclass(control, slash_control_subclass_proc, 6,
            reinterpret_cast<DWORD_PTR>(this));
    }
    InvalidateRect(slashPopup_, nullptr, TRUE);
    if (slashRowsEdit_) {
        SetFocus(slashRowsEdit_);
        SendMessageW(slashRowsEdit_, EM_SETSEL, 0, -1);
    }
}

void AppWindow::insert_table_from_form() {
    if (!slashTableMode_) return;
    wchar_t rowsText[8]{};
    wchar_t columnsText[8]{};
    GetWindowTextW(slashRowsEdit_, rowsText, static_cast<int>(std::size(rowsText)));
    GetWindowTextW(slashColumnsEdit_, columnsText, static_cast<int>(std::size(columnsText)));
    const int rows = std::clamp(_wtoi(rowsText), 2, 20);
    const int columns = std::clamp(_wtoi(columnsText), 1, 20);
    const bool inserted = editor_.insert_markdown_command("table", rows, columns);
    hide_slash_popup(inserted);
    if (!inserted) MessageBeep(MB_ICONWARNING);
    update_status();
}

void AppWindow::paint_slash_popup(HDC device) {
    if (!slashPopup_ || !device) return;
    RECT client{};
    GetClientRect(slashPopup_, &client);
    const COLORREF background = dark_ ? RGB(30, 34, 40) : RGB(255, 255, 255);
    const COLORREF foreground = dark_ ? RGB(230, 237, 243) : RGB(36, 41, 47);
    const COLORREF muted = dark_ ? RGB(139, 148, 158) : RGB(110, 118, 129);
    const COLORREF selected = dark_ ? RGB(48, 54, 61) : RGB(242, 244, 247);
    const COLORREF badge = dark_ ? RGB(55, 61, 68) : RGB(245, 245, 245);
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(device, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    SetBkMode(device, TRANSPARENT);
    SelectObject(device, uiFont_ ? uiFont_ : GetStockObject(DEFAULT_GUI_FONT));

    if (slashTableMode_) {
        SetTextColor(device, foreground);
        RECT title{16, 10, client.right - 12, 36};
        DrawTextW(device, i18n::text(L"Insert Table").c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(device, muted);
        RECT description{16, 86, client.right - 12, 112};
        DrawTextW(device, i18n::text(L"Rows include the header; range: 2–20 rows, 1–20 columns").c_str(), -1, &description,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const int end = std::min(static_cast<int>(slashVisibleCommands_.size()),
        slashScrollOffset_ + kSlashMaxVisible);
    for (int visibleIndex = slashScrollOffset_; visibleIndex < end; ++visibleIndex) {
        const int row = visibleIndex - slashScrollOffset_;
        RECT item{2, row * kSlashItemHeight + 1, client.right - 2,
            (row + 1) * kSlashItemHeight - 1};
        if (visibleIndex == slashSelected_) {
            HBRUSH selectedBrush = CreateSolidBrush(selected);
            FillRect(device, &item, selectedBrush);
            DeleteObject(selectedBrush);
        }
        const SlashCommand& command = kSlashCommands[static_cast<std::size_t>(
            slashVisibleCommands_[static_cast<std::size_t>(visibleIndex)])];
        RECT icon{10, item.top + 10, 42, item.top + 42};
        HBRUSH badgeBrush = CreateSolidBrush(badge);
        HPEN badgePen = CreatePen(PS_SOLID, 1, dark_ ? RGB(70, 76, 84) : RGB(235, 235, 235));
        const HGDIOBJ oldBrush = SelectObject(device, badgeBrush);
        const HGDIOBJ oldPen = SelectObject(device, badgePen);
        RoundRect(device, icon.left, icon.top, icon.right, icon.bottom, 8, 8);
        SelectObject(device, oldBrush);
        SelectObject(device, oldPen);
        DeleteObject(badgeBrush);
        DeleteObject(badgePen);
        SetTextColor(device, muted);
        DrawTextW(device, command.glyph, -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(device, foreground);
        RECT label{52, item.top + 6, client.right - 16, item.top + 27};
        const std::wstring localizedLabel = i18n::text(command.label);
        DrawTextW(device, localizedLabel.c_str(), -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(device, muted);
        RECT description{52, item.top + 27, client.right - 16, item.bottom - 4};
        const std::wstring localizedDescription = i18n::text(command.description);
        DrawTextW(device, localizedDescription.c_str(), -1, &description,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (static_cast<int>(slashVisibleCommands_.size()) > kSlashMaxVisible) {
        const int trackHeight = client.bottom - 12;
        const int thumbHeight = std::max(18, trackHeight * kSlashMaxVisible /
            static_cast<int>(slashVisibleCommands_.size()));
        const int maximumOffset = static_cast<int>(slashVisibleCommands_.size()) - kSlashMaxVisible;
        const int thumbTop = 6 + (trackHeight - thumbHeight) * slashScrollOffset_ /
            std::max(1, maximumOffset);
        RECT thumb{client.right - 5, thumbTop, client.right - 2, thumbTop + thumbHeight};
        HBRUSH thumbBrush = CreateSolidBrush(muted);
        FillRect(device, &thumb, thumbBrush);
        DeleteObject(thumbBrush);
    }
}

std::filesystem::path AppWindow::history_path() const {
    const std::filesystem::path binary = module_path(instance_);
    if (binary.empty()) return {};
    return binary.parent_path() / L"EditMdView.history.ini";
}

void AppWindow::populate_history_combo(HWND combo, const std::vector<std::wstring>& entries,
    std::wstring_view current) {
    if (!combo) return;
    const std::wstring preserved = current.empty() ? window_text(combo) : std::wstring(current);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& entry : entries) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.c_str()));
    }
    SetWindowTextW(combo, preserved.c_str());
}

bool AppWindow::delete_selected_history(HWND control) {
    if (!control) return false;
    HWND combo = control;
    wchar_t className[32]{};
    GetClassNameW(combo, className, static_cast<int>(std::size(className)));
    if (_wcsicmp(className, L"Edit") == 0) combo = GetParent(combo);

    std::vector<std::wstring>* entries = nullptr;
    if (combo == findBox_ || combo == replaceFindCombo_) entries = &findHistory_;
    else if (combo == replaceWithCombo_) entries = &replaceHistory_;
    if (!entries) return false;

    const LRESULT selectedIndex = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR) return false;
    const LRESULT textLength = SendMessageW(combo, CB_GETLBTEXTLEN,
        static_cast<WPARAM>(selectedIndex), 0);
    if (textLength == CB_ERR) return false;
    std::wstring selected(static_cast<std::size_t>(textLength + 1), L'\0');
    if (SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selectedIndex),
            reinterpret_cast<LPARAM>(selected.data())) == CB_ERR) {
        return false;
    }
    selected.resize(static_cast<std::size_t>(textLength));

    auto existing = std::find(entries->begin(), entries->end(), selected);
    if (existing == entries->end()) return false;
    const bool wasDropped = SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;
    const std::size_t erasedIndex = static_cast<std::size_t>(existing - entries->begin());
    entries->erase(existing);

    const std::wstring toolbarFind = window_text(findBox_);
    const std::wstring dialogFind = window_text(replaceFindCombo_);
    const std::wstring dialogReplace = window_text(replaceWithCombo_);
    populate_history_combo(findBox_, findHistory_, toolbarFind);
    if (replaceFindCombo_) populate_history_combo(replaceFindCombo_, findHistory_, dialogFind);
    if (replaceWithCombo_) populate_history_combo(replaceWithCombo_, replaceHistory_, dialogReplace);

    if (entries->empty()) {
        SetWindowTextW(combo, L"");
    } else {
        const std::size_t nextIndex = std::min(erasedIndex, entries->size() - 1);
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(nextIndex), 0);
        if (wasDropped) SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
    }
    save_search_history();
    return true;
}

void AppWindow::load_search_history() {
    findHistory_.clear();
    replaceHistory_.clear();
    const std::filesystem::path path = history_path();
    if (path.empty()) return;
    std::ifstream input(path, std::ios::binary);
    if (!input) return;

    auto append_unique = [](std::vector<std::wstring>& entries, std::wstring value) {
        if (value.empty() || value.size() > 4096 || entries.size() >= kHistoryLimit ||
            std::find(entries.begin(), entries.end(), value) != entries.end()) return;
        entries.push_back(std::move(value));
    };
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[1] != '\t' || line.size() > 65536) continue;
        std::wstring value = unescape_history(std::string_view(line).substr(2));
        if (line[0] == 'F') append_unique(findHistory_, std::move(value));
        else if (line[0] == 'R') append_unique(replaceHistory_, std::move(value));
    }
}

void AppWindow::remember_search_history(std::wstring_view find,
    std::optional<std::wstring_view> replacement) {
    bool changed = false;
    auto remember = [&](std::vector<std::wstring>& entries, std::wstring_view value) {
        if (value.empty() || value.size() > 4096) return;
        const auto existing = std::find(entries.begin(), entries.end(), value);
        if (existing != entries.end() && existing == entries.begin()) return;
        if (existing != entries.end()) entries.erase(existing);
        entries.insert(entries.begin(), std::wstring(value));
        if (entries.size() > kHistoryLimit) entries.resize(kHistoryLimit);
        changed = true;
    };
    remember(findHistory_, find);
    if (replacement) remember(replaceHistory_, *replacement);
    if (!changed) return;

    populate_history_combo(findBox_, findHistory_, find);
    if (replaceFindCombo_) populate_history_combo(replaceFindCombo_, findHistory_, find);
    if (replaceWithCombo_) {
        const std::wstring currentReplacement = window_text(replaceWithCombo_);
        populate_history_combo(replaceWithCombo_, replaceHistory_, currentReplacement);
    }
    save_search_history();
}

void AppWindow::save_search_history() {
    const std::filesystem::path target = history_path();
    if (target.empty()) return;
    std::filesystem::path temporary = target;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L".tmp";

    bool succeeded = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            output << "# EditMdView search history v1\r\n";
            for (const auto& entry : findHistory_) output << "F\t" << escape_history(entry) << "\r\n";
            for (const auto& entry : replaceHistory_) output << "R\t" << escape_history(entry) << "\r\n";
            output.close();
            if (output) {
                succeeded = MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            }
        }
    }
    if (!succeeded) {
        DeleteFileW(temporary.c_str());
        if (!historyWriteWarned_) {
            historyWriteWarned_ = true;
            MessageBoxW(window_,
                i18n::text(L"Unable to write search history to the plugin folder.\n\nHistory remains available in this window. Make the plugin folder writable to keep it permanently.").c_str(),
                L"EditMdView", MB_OK | MB_ICONWARNING);
        }
    }
}

std::wstring AppWindow::document_view_state_key(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
}

void AppWindow::remember_document_view_state() {
    if (document_.path().empty() || !editor_.handle()) return;
    constexpr std::size_t maximumRememberedDocuments = 128;
    const std::wstring key = document_view_state_key(document_.path());
    DocumentViewState state;
    state.editor = editor_.view_state();
    state.previewScrollFraction = preview_.scroll_fraction();
    state.splitRatio = splitRatio_;
    state.mode = mode_;
    documentViewStates_.insert_or_assign(key, state);

    PersistedViewState persisted;
    persisted.anchor = state.editor.anchor;
    persisted.caret = state.editor.caret;
    persisted.firstVisibleLine = state.editor.firstVisibleLine;
    persisted.topVisiblePosition = state.editor.topVisiblePosition;
    persisted.horizontalOffset = state.editor.horizontalOffset;
    persisted.previewScrollFraction = state.previewScrollFraction;
    persisted.splitRatio = state.splitRatio;
    persisted.mode = static_cast<int>(state.mode);
    save_persisted_view_state(runtimeDataDirectory_, document_.path(), persisted);

    std::erase(documentViewStateOrder_, key);
    documentViewStateOrder_.push_back(key);
    while (documentViewStateOrder_.size() > maximumRememberedDocuments) {
        documentViewStates_.erase(documentViewStateOrder_.front());
        documentViewStateOrder_.erase(documentViewStateOrder_.begin());
    }
}

std::optional<DocumentViewState> AppWindow::stored_document_view_state(
    const std::filesystem::path& path) {
    const std::wstring key = document_view_state_key(path);
    if (const auto found = documentViewStates_.find(key); found != documentViewStates_.end()) {
        return found->second;
    }
    const auto persisted = load_persisted_view_state(runtimeDataDirectory_, path);
    if (!persisted) return std::nullopt;
    DocumentViewState state;
    state.editor.anchor = persisted->anchor;
    state.editor.caret = persisted->caret;
    state.editor.firstVisibleLine = persisted->firstVisibleLine;
    state.editor.topVisiblePosition = persisted->topVisiblePosition;
    state.editor.horizontalOffset = persisted->horizontalOffset;
    state.previewScrollFraction = persisted->previewScrollFraction;
    state.splitRatio = persisted->splitRatio;
    state.mode = static_cast<ViewMode>(persisted->mode);
    documentViewStates_.insert_or_assign(key, state);
    std::erase(documentViewStateOrder_, key);
    documentViewStateOrder_.push_back(key);
    return state;
}

bool AppWindow::offer_recovery_snapshot() {
    const auto recovered = load_recovery_snapshot(runtimeDataDirectory_, document_.path());
    if (!recovered) return false;
    if (*recovered == document_.text()) {
        remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
        return false;
    }
    const std::wstring prompt = i18n::text(L"Unsaved recovery draft detected:") + L" " +
        file_title(document_.path()) + L"\n\n" +
        i18n::text(L"Restore it? Choosing No deletes the recovery draft.");
    const int answer = MessageBoxW(window_, prompt.c_str(),
        i18n::text(L"Recover Unsaved Content").c_str(),
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (answer != IDYES) {
        remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
        return false;
    }
    editor_.restore_unsaved_text(*recovered);
    recoverySnapshotContent_ = *recovered;
    recoveryWriteFailed_ = false;
    return true;
}

void AppWindow::write_recovery_snapshot() {
    if (!editor_.handle() || document_.path().empty()) return;
    if (!editor_.modified()) {
        recoverySnapshotContent_.clear();
        recoveryWriteFailed_ = false;
        remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
        update_status();
        return;
    }
    const std::string content = editor_.text();
    if (content == recoverySnapshotContent_ && !recoveryWriteFailed_) return;
    if (save_recovery_snapshot(runtimeDataDirectory_, document_.path(), content)) {
        recoverySnapshotContent_ = content;
        recoveryWriteFailed_ = false;
    } else {
        recoveryWriteFailed_ = true;
    }
    update_status();
}

bool AppWindow::load_file(const std::filesystem::path& path) {
    if (!confirm_discard_or_save()) return false;
    if (editor_.modified()) {
        remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
    }
    std::wstring error;
    Document next;
    if (!next.load(path, error)) {
        MessageBoxW(window_, error.c_str(), L"EditMdView", MB_OK | MB_ICONERROR);
        return false;
    }
    SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(editor_.handle(), WM_SETREDRAW, FALSE, 0);
    if (preview_.handle()) SendMessageW(preview_.handle(), WM_SETREDRAW, FALSE, 0);
    remember_document_view_state();
    document_ = std::move(next);
    recoverySnapshotContent_.clear();
    recoveryWriteFailed_ = false;
    discardingChanges_ = false;
    externalChangeNotified_ = false;
    lastPreviewFind_.clear();
    previewFindStatus_.clear();
    const auto saved = stored_document_view_state(document_.path());
    const bool hasSavedState = saved.has_value();
    const DocumentViewState restored = saved.value_or(DocumentViewState{});
    if (hasSavedState) splitRatio_ = std::clamp(restored.splitRatio, 0.1, 0.9);
    ViewMode nextMode = document_.supports_preview()
        ? (hasSavedState ? restored.mode : ViewMode::Preview)
        : ViewMode::Edit;
    restoringDocumentState_ = true;
    auto properties = SciteProperties::load_for_document(
        module_path(instance_), document_.path(), dark_);
    configurationSignature_ = properties.configuration_signature();
    editor_.set_properties(std::move(properties));
    editor_.set_text(document_.text(), document_.path(), document_.eol_name());
    const bool recovered = offer_recovery_snapshot();
    if (recovered) nextMode = ViewMode::Edit;
    if (document_.supports_preview() && !preview_.handle()) {
        preview_.create(window_, instance_, error);
        preview_.set_find_shortcuts(window_, kMessageFocusFind, kMessageFindNext,
            kMessagePreviewFindResult, kMessageToggleMode, kMessageReloadConfiguration,
            kMessageSave, kMessageSaveAs);
        preview_.set_source_navigation(window_, kMessagePreviewLocate);
    }
    set_mode(nextMode);
    refresh_preview(hasSavedState ? std::optional<double>(restored.previewScrollFraction)
                                  : std::optional<double>(0.0));
    if (hasSavedState) editor_.restore_view_state(restored.editor);
    KillTimer(window_, kPreviewTimer);
    KillTimer(window_, kPreviewSyncTimer);
    SetTimer(window_, kExternalChangeTimer, kExternalChangeDelayMs, nullptr);
    restoringDocumentState_ = false;
    update_status();
    SendMessageW(editor_.handle(), WM_SETREDRAW, TRUE, 0);
    ShowWindow(editor_.handle(), mode_ == ViewMode::Preview ? SW_HIDE : SW_SHOW);
    if (preview_.handle()) {
        SendMessageW(preview_.handle(), WM_SETREDRAW, TRUE, 0);
        ShowWindow(preview_.handle(), document_.supports_preview() && mode_ != ViewMode::Edit
            ? SW_SHOW : SW_HIDE);
    }
    SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(window_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return true;
}

void AppWindow::close() {
    if (closing_) return;
    if (!confirm_discard_or_save()) return;
    discardingChanges_ = editor_.modified();
    if (discardingChanges_) {
        recoverySnapshotContent_.clear();
        remove_recovery_snapshot(runtimeDataDirectory_, document_.path());
    }
    remember_document_view_state();
    closing_ = true;
    DestroyWindow(window_);
}

bool AppWindow::search(const std::wstring& text, int flags) {
    SetWindowTextW(findBox_, text.c_str());
    remember_search_history(text);
    if (mode_ != ViewMode::Edit) {
        const bool fromStart = (flags & LCS_FIND_FIRST) != 0 || text != lastPreviewFind_;
        lastPreviewFind_ = text;
        preview_.find(text, (flags & LCS_BACKWARDS) != 0, fromStart);
        if (mode_ == ViewMode::Preview) return !text.empty();
    }
    return editor_.find(wide_to_utf8(text), (flags & LCS_MATCH_CASE) != 0,
        (flags & LCS_WHOLE_WORDS) != 0, (flags & LCS_BACKWARDS) != 0,
        (flags & LCS_FIND_FIRST) != 0);
}

int AppWindow::send_command(int command, int) {
    switch (command) {
    case LC_COPY:
        editor_.copy();
        return LISTPLUGIN_OK;
    case LC_SELECTALL:
        editor_.select_all();
        return LISTPLUGIN_OK;
    case LC_NEWPARAMS:
        return LISTPLUGIN_OK;
    default:
        return LISTPLUGIN_ERROR;
    }
}

} // namespace editmdview
