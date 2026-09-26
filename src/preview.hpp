#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace editmdview {

class Preview {
public:
    Preview();
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    bool create(HWND parent, HINSTANCE instance, std::wstring& error);
    void destroy();
    void resize(const RECT& bounds) const;
    void show(bool visible) const;
    void set_content(std::wstring html, const std::filesystem::path& documentFolder,
        std::optional<double> initialScrollFraction = std::nullopt);
    void scroll_to_fraction(double fraction);
    double scroll_fraction() const noexcept;
    void find(std::wstring_view query, bool backwards, bool fromStart);
    void focus() const;
    void set_find_shortcuts(HWND commandTarget, UINT focusFindMessage, UINT findNextMessage,
        UINT findResultMessage = 0, UINT toggleModeMessage = 0,
        UINT reloadConfigurationMessage = 0, UINT saveMessage = 0,
        UINT saveAsMessage = 0);
    void set_source_navigation(HWND commandTarget, UINT locateSourceMessage);
    void set_source_navigation_enabled(bool enabled);
    bool ready() const noexcept;
    HWND handle() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace editmdview
