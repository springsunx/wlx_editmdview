#include "app_window.hpp"
#include "text_detection.hpp"
#include "wlx_api.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace {

HINSTANCE g_instance = nullptr;

std::wstring ansi_to_wide(const char* text) {
    if (!text) return {};
    const int required = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (required <= 1) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), required);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

} // namespace

namespace Scintilla::Internal {
int ResourcesRelease(bool fromDllMain) noexcept;
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // Total Commander can unload and immediately reload a WLX plugin in
        // the same process. Release every registered window class whose
        // procedure points into this DLL before its code is unmapped.
        editmdview::AppWindow::unregister_classes(instance);
        Scintilla::Internal::ResourcesRelease(true);
        g_instance = nullptr;
    }
    return TRUE;
}

WLX_EXPORT HWND WLX_CALL ListLoadW(HWND parentWin, wchar_t* fileToLoad, int showFlags) {
    if (!parentWin || !fileToLoad) return nullptr;
    const std::filesystem::path path(fileToLoad);
    if (!editmdview::is_supported_text_file(path)) return nullptr;
    return editmdview::AppWindow::create(parentWin, g_instance, path, showFlags);
}

WLX_EXPORT HWND WLX_CALL ListLoad(HWND parentWin, char* fileToLoad, int showFlags) {
    std::wstring path = ansi_to_wide(fileToLoad);
    return path.empty() ? nullptr : ListLoadW(parentWin, path.data(), showFlags);
}

WLX_EXPORT int WLX_CALL ListLoadNextW(HWND, HWND pluginWin, wchar_t* fileToLoad, int) {
    auto* app = editmdview::AppWindow::from(pluginWin);
    if (!app || !fileToLoad) return LISTPLUGIN_ERROR;
    const std::filesystem::path path(fileToLoad);
    return editmdview::is_supported_text_file(path) && app->load_file(path)
        ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

WLX_EXPORT int WLX_CALL ListLoadNext(HWND parentWin, HWND pluginWin, char* fileToLoad, int showFlags) {
    std::wstring path = ansi_to_wide(fileToLoad);
    return path.empty() ? LISTPLUGIN_ERROR : ListLoadNextW(parentWin, pluginWin, path.data(), showFlags);
}

WLX_EXPORT void WLX_CALL ListCloseWindow(HWND listWin) {
    if (auto* app = editmdview::AppWindow::from(listWin)) app->close();
}

WLX_EXPORT void WLX_CALL ListGetDetectString(char* detectString, int maxLength) {
    if (!detectString || maxLength <= 0) return;
    // An empty expression asks Total Commander to call ListLoadW for any file.
    // ListLoadW then performs the more reliable content-based text check and
    // returns nullptr for binary files, allowing the next WLX plugin to try.
    detectString[0] = '\0';
}

WLX_EXPORT int WLX_CALL ListSearchTextW(HWND listWin, wchar_t* searchString, int searchParameter) {
    auto* app = editmdview::AppWindow::from(listWin);
    return app && searchString && app->search(searchString, searchParameter) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

WLX_EXPORT int WLX_CALL ListSearchText(HWND listWin, char* searchString, int searchParameter) {
    std::wstring query = ansi_to_wide(searchString);
    return query.empty() ? LISTPLUGIN_ERROR : ListSearchTextW(listWin, query.data(), searchParameter);
}

WLX_EXPORT int WLX_CALL ListSendCommand(HWND listWin, int command, int parameter) {
    auto* app = editmdview::AppWindow::from(listWin);
    return app ? app->send_command(command, parameter) : LISTPLUGIN_ERROR;
}

WLX_EXPORT void WLX_CALL ListSetDefaultParams(ListDefaultParamStruct*) {}
