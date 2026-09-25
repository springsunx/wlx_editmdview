#include "preview.hpp"
#include "resource.h"

#include <unknwn.h>
#include <WebView2.h>

#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace editmdview {

namespace {
std::wstring javascript_string(std::wstring_view value);
std::wstring load_utf8_resource(HINSTANCE instance, int resourceId);
bool write_clipboard_text(HWND owner, std::wstring_view text);
}

struct Preview::State : std::enable_shared_from_this<Preview::State> {
    HWND host = nullptr;
    bool alive = true;
    bool visible = true;
    std::wstring pendingHtml;
    std::filesystem::path pendingFolder;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webView;
    EventRegistrationToken navigationToken{};
    EventRegistrationToken navigationCompletedToken{};
    EventRegistrationToken acceleratorToken{};
    EventRegistrationToken webMessageToken{};
    double pendingScrollFraction = 0.0;
    double currentScrollFraction = 0.0;
    bool navigationPending = false;
    bool documentDisplayed = false;
    HWND commandTarget = nullptr;
    UINT focusFindMessage = 0;
    UINT findNextMessage = 0;
    UINT findResultMessage = 0;
    UINT toggleModeMessage = 0;
    UINT reloadConfigurationMessage = 0;
    UINT locateSourceMessage = 0;
    bool sourceNavigationEnabled = false;
    std::wstring previewScript;
    std::wstring previewStyle;
    std::wstring highlightScript;
    bool runtimeInjected = false;
    bool runtimeInjectionPending = false;
    unsigned long documentGeneration = 0;
    void update_bounds() const {
        if (!host || !controller) return;
        RECT bounds{};
        GetClientRect(host, &bounds);
        controller->put_Bounds(bounds);
    }

    void display_pending() {
        if (!webView || pendingHtml.empty()) return;
        ComPtr<ICoreWebView2_3> webView3;
        if (SUCCEEDED(webView.As(&webView3)) && !pendingFolder.empty()) {
            webView3->SetVirtualHostNameToFolderMapping(L"editmdview.local", pendingFolder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
        navigationPending = true;
        if (!documentDisplayed) {
            ++documentGeneration;
            runtimeInjected = false;
            runtimeInjectionPending = false;
            webView->NavigateToString(pendingHtml.c_str());
            return;
        }

        const int scaledFraction = static_cast<int>(std::lround(
            std::clamp(pendingScrollFraction, 0.0, 1.0) * 1'000'000.0));
        const std::wstring script =
            L"(()=>{const html=" + javascript_string(pendingHtml) +
            L";const next=new DOMParser().parseFromString(html,'text/html');"
            L"const root=document.documentElement;"
            L"for(const attribute of Array.from(root.attributes))root.removeAttribute(attribute.name);"
            L"for(const attribute of Array.from(next.documentElement.attributes))"
                L"root.setAttribute(attribute.name,attribute.value);"
            L"document.head.replaceChildren(...Array.from(next.head.childNodes,"
                L"node=>document.importNode(node,true)));"
            L"document.body.replaceWith(document.importNode(next.body,true));"
            L"const d=document.documentElement,b=document.body;"
            L"const h=Math.max(d?d.scrollHeight:0,b?b.scrollHeight:0);"
            L"const m=Math.max(0,h-window.innerHeight);"
            L"window.scrollTo(0,m*(" + std::to_wstring(scaledFraction) + L"/1000000));"
            L"return true;})()";
        std::weak_ptr<Preview::State> weakState = shared_from_this();
        webView->ExecuteScript(script.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [weakState](HRESULT result, LPCWSTR) -> HRESULT {
                    const auto current = weakState.lock();
                    if (!current || !current->alive) return S_OK;
                    if (FAILED(result)) {
                        current->documentDisplayed = false;
                        current->display_pending();
                        return S_OK;
                    }
                    current->navigationPending = false;
                    current->currentScrollFraction = current->pendingScrollFraction;
                    current->install_preview_features();
                    return S_OK;
                }).Get());
    }

    void apply_pending_scroll() {
        if (!webView) return;
        const int scaledFraction = static_cast<int>(std::lround(
            std::clamp(pendingScrollFraction, 0.0, 1.0) * 1'000'000.0));
        const std::wstring script =
            L"(()=>{const d=document.documentElement,b=document.body;"
            L"const h=Math.max(d?d.scrollHeight:0,b?b.scrollHeight:0);"
            L"const m=Math.max(0,h-window.innerHeight);"
            L"window.scrollTo(0,m*(" + std::to_wstring(scaledFraction) + L"/1000000));})()";
        webView->ExecuteScript(script.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
    }

    void apply_preview_features() {
        if (!webView || !documentDisplayed || navigationPending || !runtimeInjected) return;
        std::wstring script =
            L"(()=>{let style=document.getElementById('editmdview-preview-style');"
            L"if(!style){style=document.createElement('style');style.id='editmdview-preview-style';"
            L"(document.head||document.documentElement).append(style);}style.textContent=" +
            javascript_string(previewStyle) +
            L";return window.EditMdViewPreview?window.EditMdViewPreview.install({sourceNavigationEnabled:" +
            std::wstring(sourceNavigationEnabled ? L"true" : L"false") + L"}):false;})()";
        std::weak_ptr<Preview::State> weakState = shared_from_this();
        webView->ExecuteScript(script.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [weakState](HRESULT result, LPCWSTR) -> HRESULT {
                    const auto current = weakState.lock();
                    if (current && current->alive && SUCCEEDED(result)) current->apply_pending_scroll();
                    return S_OK;
                }).Get());
    }

    void inject_preview_runtime(unsigned long generation) {
        std::weak_ptr<Preview::State> weakState = shared_from_this();
        webView->ExecuteScript(previewScript.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [weakState, generation](HRESULT result, LPCWSTR) -> HRESULT {
                    const auto current = weakState.lock();
                    if (!current || !current->alive || current->documentGeneration != generation) return S_OK;
                    current->runtimeInjectionPending = false;
                    if (FAILED(result)) return S_OK;
                    current->runtimeInjected = true;
                    current->apply_preview_features();
                    return S_OK;
                }).Get());
    }

    void install_preview_features() {
        if (!webView || !documentDisplayed || navigationPending) return;
        if (runtimeInjected) {
            apply_preview_features();
            return;
        }
        if (runtimeInjectionPending) return;
        runtimeInjectionPending = true;
        const unsigned long generation = documentGeneration;
        std::weak_ptr<Preview::State> weakState = shared_from_this();
        webView->ExecuteScript(highlightScript.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [weakState, generation](HRESULT, LPCWSTR) -> HRESULT {
                    const auto current = weakState.lock();
                    if (!current || !current->alive || current->documentGeneration != generation) return S_OK;
                    current->inject_preview_runtime(generation);
                    return S_OK;
                }).Get());
    }
};

namespace {

std::filesystem::path user_data_folder() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        return {};
    }
    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    path /= L"EditMdView";
    path /= L"WebView2";
    std::wstring executable(32768, L'\0');
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (executableLength > 0 && executableLength < executable.size()) {
        executable.resize(executableLength);
        const std::wstring hostName = std::filesystem::path(executable).stem().wstring();
        if (!hostName.empty()) path /= hostName;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

bool starts_with(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::wstring javascript_string(std::wstring_view value) {
    std::wstring encoded = L"\"";
    encoded.reserve(value.size() + 2);
    for (wchar_t character : value) {
        switch (character) {
        case L'\\': encoded += L"\\\\"; break;
        case L'\"': encoded += L"\\\""; break;
        case L'\b': encoded += L"\\b"; break;
        case L'\f': encoded += L"\\f"; break;
        case L'\n': encoded += L"\\n"; break;
        case L'\r': encoded += L"\\r"; break;
        case L'\t': encoded += L"\\t"; break;
        default:
            if (character < 0x20 || character == 0x2028 || character == 0x2029) {
                wchar_t escape[7]{};
                swprintf_s(escape, L"\\u%04X", static_cast<unsigned>(character));
                encoded += escape;
            } else {
                encoded += character;
            }
            break;
        }
    }
    encoded += L'\"';
    return encoded;
}

std::wstring load_utf8_resource(HINSTANCE instance, int resourceId) {
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return {};
    const HGLOBAL loaded = LoadResource(instance, resource);
    if (!loaded) return {};
    const DWORD byteCount = SizeofResource(instance, resource);
    const auto* bytes = static_cast<const char*>(LockResource(loaded));
    if (!bytes || byteCount == 0 || byteCount > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        return {};
    }
    const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes,
        static_cast<int>(byteCount), nullptr, 0);
    if (wideCount <= 0) return {};
    std::wstring value(static_cast<size_t>(wideCount), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(byteCount),
        value.data(), wideCount) != wideCount) {
        return {};
    }
    return value;
}

bool write_clipboard_text(HWND owner, std::wstring_view text) {
    if (text.size() > (std::numeric_limits<SIZE_T>::max() / sizeof(wchar_t)) - 1) return false;
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    auto* buffer = static_cast<wchar_t*>(GlobalLock(memory));
    if (!buffer) {
        GlobalFree(memory);
        return false;
    }
    std::copy(text.begin(), text.end(), buffer);
    buffer[text.size()] = L'\0';
    GlobalUnlock(memory);

    bool opened = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        Sleep(4);
    }
    if (!opened) {
        GlobalFree(memory);
        return false;
    }
    const bool transferred = EmptyClipboard() != FALSE &&
        SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!transferred) GlobalFree(memory);
    return transferred;
}

} // namespace

Preview::Preview() = default;
Preview::~Preview() { destroy(); }

bool Preview::create(HWND parent, HINSTANCE instance, std::wstring& error) {
    state_ = std::make_shared<State>();
    state_->previewScript = load_utf8_resource(instance, IDR_PREVIEW_SCRIPT);
    state_->previewStyle = load_utf8_resource(instance, IDR_PREVIEW_STYLE);
    state_->highlightScript = load_utf8_resource(instance, IDR_HIGHLIGHT_SCRIPT);
    for (const int resourceId : {IDR_HIGHLIGHT_CMAKE_SCRIPT, IDR_HIGHLIGHT_DOS_SCRIPT,
            IDR_HIGHLIGHT_LISP_SCRIPT, IDR_HIGHLIGHT_POWERSHELL_SCRIPT,
            IDR_HIGHLIGHT_PROPERTIES_SCRIPT}) {
        std::wstring languageScript = load_utf8_resource(instance, resourceId);
        if (languageScript.empty()) {
            error = L"无法读取内嵌的预览资源。";
            state_.reset();
            return false;
        }
        state_->highlightScript += L'\n';
        state_->highlightScript += std::move(languageScript);
    }
    if (state_->previewScript.empty() || state_->previewStyle.empty() || state_->highlightScript.empty()) {
        error = L"无法读取内嵌的预览资源。";
        state_.reset();
        return false;
    }
    state_->host = CreateWindowExW(0, L"STATIC", L"正在启动渲染预览…",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_CENTER,
        0, 0, 100, 100, parent, nullptr, instance, nullptr);
    if (!state_->host) {
        error = L"无法创建预览区域。";
        state_.reset();
        return false;
    }

    const auto dataFolder = user_data_folder();
    const auto state = state_;
    const HRESULT startResult = CreateCoreWebView2EnvironmentWithOptions(nullptr,
        dataFolder.empty() ? nullptr : dataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (!state->alive) return S_OK;
                if (FAILED(result) || !environment) {
                    SetWindowTextW(state->host, L"未检测到 WebView2 Runtime，当前仅可使用编辑模式。\r\n可安装 Microsoft Edge WebView2 Runtime 后重试。");
                    return S_OK;
                }
                state->environment = environment;
                return environment->CreateCoreWebView2Controller(state->host,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [state](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!state->alive) return S_OK;
                            if (FAILED(controllerResult) || !controller) {
                                SetWindowTextW(state->host, L"渲染预览初始化失败，当前仅可使用编辑模式。");
                                return S_OK;
                            }
                            state->controller = controller;
                            controller->get_CoreWebView2(&state->webView);
                            if (!state->webView) return S_OK;

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(state->webView->get_Settings(&settings)) && settings) {
                                // Preview documents install a restrictive CSP before their content.
                                // Script support remains on so host-injected event listeners can run.
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                ComPtr<ICoreWebView2Settings3> settings3;
                                if (SUCCEEDED(settings.As(&settings3)) && settings3) {
                                    settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                                }
                            }

                            std::weak_ptr<Preview::State> weakState = state;
                            state->webView->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [weakState](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        const auto current = weakState.lock();
                                        if (!current || !current->alive) return S_OK;
                                        LPWSTR uri = nullptr;
                                        if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
                                        const std::wstring_view value(uri);
                                        constexpr std::wstring_view locatePrefix = L"editmdview://locate/";
                                        if (starts_with(value, locatePrefix)) {
                                            args->put_Cancel(TRUE);
                                            const std::wstring_view location = value.substr(locatePrefix.size());
                                            const bool exactLine = starts_with(location, L"line/");
                                            const std::wstring_view number = exactLine
                                                ? location.substr(std::wstring_view(L"line/").size())
                                                : (starts_with(location, L"fraction/")
                                                    ? location.substr(std::wstring_view(L"fraction/").size())
                                                    : std::wstring_view{});
                                            if (!number.empty() && number.size() < 16 && current->commandTarget &&
                                                current->locateSourceMessage != 0) {
                                                std::wstring copy(number);
                                                wchar_t* end = nullptr;
                                                const long parsed = std::wcstol(copy.c_str(), &end, 10);
                                                const long maximum = exactLine ? std::numeric_limits<int>::max() : 1'000'000;
                                                if (end != copy.c_str() && *end == L'\0' && parsed >= 0 && parsed <= maximum) {
                                                    PostMessageW(current->commandTarget, current->locateSourceMessage,
                                                        exactLine ? 1 : 0, static_cast<LPARAM>(parsed));
                                                }
                                            }
                                            CoTaskMemFree(uri);
                                            return S_OK;
                                        }
                                        BOOL userInitiated = FALSE;
                                        if (FAILED(args->get_IsUserInitiated(&userInitiated)) || !userInitiated) {
                                            CoTaskMemFree(uri);
                                            return S_OK;
                                        }
                                        if (value == L"about:blank") {
                                            CoTaskMemFree(uri);
                                            return S_OK;
                                        }
                                        args->put_Cancel(TRUE);
                                        if (starts_with(value, L"https://") || starts_with(value, L"http://") ||
                                            starts_with(value, L"mailto:")) {
                                            ShellExecuteW(current->host, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
                                        }
                                        CoTaskMemFree(uri);
                                        return S_OK;
                                    }).Get(), &state->navigationToken);

                            state->webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [weakState](ICoreWebView2*,
                                        ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        const auto current = weakState.lock();
                                        if (current && current->alive) {
                                            current->navigationPending = false;
                                            current->documentDisplayed = true;
                                            current->install_preview_features();
                                        }
                                        return S_OK;
                                    }).Get(), &state->navigationCompletedToken);

                            state->webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [weakState](ICoreWebView2*,
                                        ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        const auto current = weakState.lock();
                                        if (!current || !current->alive || current->navigationPending) return S_OK;
                                        LPWSTR message = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                            const std::wstring_view value(message);
                                            constexpr std::wstring_view copyPrefix = L"editmdview-copy-code:";
                                            constexpr std::wstring_view scrollPrefix = L"editmdview-scroll:";
                                            constexpr std::wstring_view findPrefix = L"editmdview-find-result:";
                                            if (value.starts_with(copyPrefix)) {
                                                const bool copied = write_clipboard_text(current->host,
                                                    value.substr(copyPrefix.size()));
                                                current->webView->PostWebMessageAsString(copied
                                                    ? L"editmdview-copy-result:ok"
                                                    : L"editmdview-copy-result:failed");
                                            } else if (value.starts_with(scrollPrefix)) {
                                                wchar_t* end = nullptr;
                                                const double fraction = std::wcstod(message + scrollPrefix.size(), &end);
                                                if (end != message + scrollPrefix.size() && std::isfinite(fraction)) {
                                                    current->currentScrollFraction = std::clamp(fraction, 0.0, 1.0);
                                                    current->pendingScrollFraction = current->currentScrollFraction;
                                                }
                                            } else if (value.starts_with(findPrefix) && current->commandTarget &&
                                                current->findResultMessage != 0) {
                                                const wchar_t* first = message + findPrefix.size();
                                                wchar_t* separator = nullptr;
                                                const long index = std::wcstol(first, &separator, 10);
                                                wchar_t* end = nullptr;
                                                const long total = separator && *separator == L'/'
                                                    ? std::wcstol(separator + 1, &end, 10) : -1;
                                                if (separator && *separator == L'/' && end && *end == L'\0' &&
                                                    index >= 0 && total >= 0 && index <= total &&
                                                    total <= std::numeric_limits<int>::max()) {
                                                    PostMessageW(current->commandTarget, current->findResultMessage,
                                                        static_cast<WPARAM>(index), static_cast<LPARAM>(total));
                                                }
                                            }
                                            CoTaskMemFree(message);
                                        }
                                        return S_OK;
                                    }).Get(), &state->webMessageToken);

                            state->controller->add_AcceleratorKeyPressed(
                                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [weakState](ICoreWebView2Controller*,
                                        ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        const auto current = weakState.lock();
                                        if (!current || !current->alive || !current->commandTarget) return S_OK;
                                        COREWEBVIEW2_KEY_EVENT_KIND kind{};
                                        UINT key = 0;
                                        if (FAILED(args->get_KeyEventKind(&kind)) ||
                                            FAILED(args->get_VirtualKey(&key)) ||
                                            (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                                                kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)) return S_OK;
                                        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                                        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                                        if (key == 'R' && control && shift &&
                                            current->reloadConfigurationMessage != 0) {
                                            PostMessageW(current->commandTarget,
                                                current->reloadConfigurationMessage, 0, 0);
                                            args->put_Handled(TRUE);
                                        } else if (key == 'M' && control && current->toggleModeMessage != 0) {
                                            PostMessageW(current->commandTarget, current->toggleModeMessage, 0, 0);
                                            args->put_Handled(TRUE);
                                        } else if (key == 'F' && control &&
                                            current->focusFindMessage != 0) {
                                            PostMessageW(current->commandTarget, current->focusFindMessage, 0, 0);
                                            args->put_Handled(TRUE);
                                        } else if (key == VK_F3 && current->findNextMessage != 0) {
                                            PostMessageW(current->commandTarget, current->findNextMessage,
                                                (GetKeyState(VK_SHIFT) & 0x8000) != 0, 0);
                                            args->put_Handled(TRUE);
                                        }
                                        return S_OK;
                                    }).Get(), &state->acceleratorToken);

                            state->controller->put_IsVisible(state->visible ? TRUE : FALSE);
                            state->update_bounds();
                            state->display_pending();
                            return S_OK;
                        }).Get());
            }).Get());

    if (FAILED(startResult)) {
        error = L"无法启动 WebView2 预览。";
        SetWindowTextW(state_->host, L"无法启动 WebView2，当前仅可使用编辑模式。");
    }
    return true;
}

void Preview::destroy() {
    if (!state_) return;
    state_->alive = false;
    if (state_->webView && state_->navigationToken.value != 0) {
        state_->webView->remove_NavigationStarting(state_->navigationToken);
    }
    if (state_->webView && state_->navigationCompletedToken.value != 0) {
        state_->webView->remove_NavigationCompleted(state_->navigationCompletedToken);
    }
    if (state_->webView && state_->webMessageToken.value != 0) {
        state_->webView->remove_WebMessageReceived(state_->webMessageToken);
    }
    if (state_->controller && state_->acceleratorToken.value != 0) {
        state_->controller->remove_AcceleratorKeyPressed(state_->acceleratorToken);
    }
    if (state_->controller) state_->controller->Close();
    state_->webView.Reset();
    state_->controller.Reset();
    state_->environment.Reset();
    if (state_->host) DestroyWindow(state_->host);
    state_.reset();
}

void Preview::resize(const RECT& bounds) const {
    if (!state_ || !state_->host) return;
    MoveWindow(state_->host, bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, TRUE);
    state_->update_bounds();
}

void Preview::show(bool visible) const {
    if (!state_) return;
    state_->visible = visible;
    if (state_->host) ShowWindow(state_->host, visible ? SW_SHOW : SW_HIDE);
    if (state_->controller) state_->controller->put_IsVisible(visible ? TRUE : FALSE);
}

void Preview::set_content(std::wstring html, const std::filesystem::path& documentFolder,
    std::optional<double> initialScrollFraction) {
    if (!state_) return;
    if (initialScrollFraction.has_value()) {
        state_->pendingScrollFraction = std::clamp(*initialScrollFraction, 0.0, 1.0);
        state_->currentScrollFraction = state_->pendingScrollFraction;
    }
    state_->navigationPending = true;
    state_->pendingHtml = std::move(html);
    state_->pendingFolder = documentFolder;
    state_->display_pending();
}

void Preview::scroll_to_fraction(double fraction) {
    if (!state_) return;
    state_->pendingScrollFraction = std::clamp(fraction, 0.0, 1.0);
    state_->currentScrollFraction = state_->pendingScrollFraction;
    state_->apply_pending_scroll();
}

double Preview::scroll_fraction() const noexcept {
    return state_ ? state_->currentScrollFraction : 0.0;
}

void Preview::find(std::wstring_view query, bool backwards, bool fromStart) {
    if (!state_ || !state_->webView || query.empty()) return;
    std::wstring script = L"window.EditMdViewPreview?window.EditMdViewPreview.find(" +
        javascript_string(query) + L"," + (backwards ? L"true" : L"false") + L"," +
        (fromStart ? L"true" : L"false") + L"):false";
    state_->webView->ExecuteScript(script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
}

void Preview::focus() const {
    if (state_ && state_->controller) {
        state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

void Preview::set_find_shortcuts(HWND commandTarget, UINT focusFindMessage, UINT findNextMessage,
    UINT findResultMessage, UINT toggleModeMessage, UINT reloadConfigurationMessage) {
    if (!state_) return;
    state_->commandTarget = commandTarget;
    state_->focusFindMessage = focusFindMessage;
    state_->findNextMessage = findNextMessage;
    state_->findResultMessage = findResultMessage;
    state_->toggleModeMessage = toggleModeMessage;
    state_->reloadConfigurationMessage = reloadConfigurationMessage;
}

void Preview::set_source_navigation(HWND commandTarget, UINT locateSourceMessage) {
    if (!state_) return;
    state_->commandTarget = commandTarget;
    state_->locateSourceMessage = locateSourceMessage;
}

void Preview::set_source_navigation_enabled(bool enabled) {
    if (!state_) return;
    state_->sourceNavigationEnabled = enabled;
    state_->install_preview_features();
}

bool Preview::ready() const noexcept { return state_ && state_->webView; }
HWND Preview::handle() const noexcept { return state_ ? state_->host : nullptr; }

} // namespace editmdview
