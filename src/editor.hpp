#pragma once

#include "scite_properties.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace editmdview {

enum class SyntaxLanguage {
    Plain, Markdown, Cpp, JavaScript, Python, Json, Html, Xml, Css, Bash,
    Sql, Yaml, Properties, Conf, CMake, Makefile, Batch, PowerShell, Rust, Lua, Lisp
};

enum class MarkdownFormat {
    Bold,
    Italic,
    Highlight,
    Strikethrough,
    InlineCode,
};

struct EditorViewState {
    std::intptr_t anchor = 0;
    std::intptr_t caret = 0;
    int firstVisibleLine = 0;
    std::intptr_t topVisiblePosition = -1;
    int horizontalOffset = 0;
};

class Editor {
public:
    bool create(HWND parent, HINSTANCE instance, bool dark, std::wstring& error);
    void destroy();
    void resize(const RECT& bounds) const;
    void show(bool visible) const;
    void focus() const;
    void set_properties(SciteProperties properties);
    void reload_configuration(const std::filesystem::path& path, bool dark);
    std::uint64_t configuration_signature() const;

    void set_text(std::string_view utf8Text, const std::filesystem::path& path, std::wstring_view eolName);
    void restore_unsaved_text(std::string_view utf8Text);
    void set_language(SyntaxLanguage language);
    void convert_eol(int scintillaEolMode);
    std::string text() const;
    std::string text_for_save();
    bool modified() const;
    void mark_saved() const;
    void set_dark(bool dark);

    void cut() const;
    void copy() const;
    void paste() const;
    void delete_selection() const;
    void select_all() const;
    void undo() const;
    void redo() const;
    bool can_undo() const;
    bool can_redo() const;
    bool has_selection() const;
    std::string selected_text() const;
    bool can_paste() const;
    void toggle_wrap();
    bool wrap_enabled() const noexcept { return wrapEnabled_; }
    void zoom_in() const;
    void zoom_out() const;
    void reset_zoom() const;
    int zoom() const;
    void go_to_line(int oneBasedLine) const;
    void toggle_fold_at(std::intptr_t position) const;
    void update_ui() const;
    void character_added(int character);
    bool find(std::string_view utf8Needle, bool matchCase, bool wholeWord, bool backwards, bool fromStart);
    bool replace_selection_if_match(std::string_view utf8Needle, std::string_view utf8Replacement,
        bool matchCase, bool wholeWord);
    int replace_all(std::string_view utf8Needle, std::string_view utf8Replacement,
        bool matchCase, bool wholeWord);
    bool markdown_shortcuts_enabled() const;
    bool markdown_slash_enabled() const;
    bool apply_markdown_format(MarkdownFormat format);
    bool insert_markdown_link();
    bool handle_markdown_enter();
    bool handle_markdown_tab(bool backwards);
    bool handle_markdown_character(int character);
    bool paste_markdown_smart();
    bool markdown_slash_context(std::intptr_t& start, std::intptr_t& end, std::string& query) const;
    bool insert_markdown_command(std::string_view command, int tableRows = 3, int tableColumns = 3);
    void cancel_auto_completion() const;
    POINT caret_screen_point() const;

    int current_line() const;
    int current_column() const;
    int line_count() const;
    int first_visible_document_line() const;
    int visible_line_count() const;
    EditorViewState view_state() const;
    void restore_view_state(const EditorViewState& state) const;
    SyntaxLanguage language() const noexcept { return language_; }
    const std::wstring& language_name() const noexcept { return languageName_; }
    std::wstring eol_name() const;
    HWND handle() const noexcept { return window_; }

private:
    LRESULT send(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) const;
    void configure_lexer(const std::filesystem::path& path);
    void configure_language(SyntaxLanguage language, bool useFileSpecificKeywords);
    void apply_rendering_settings();
    void apply_styles(bool dark);
    void apply_editor_settings();
    void apply_auto_indent(int character);
    void show_word_completion();
    void auto_close_xml_tag();

    HWND window_ = nullptr;
    bool markdown_ = false;
    bool wrapEnabled_ = true;
    int unwrappedHorizontalOffset_ = 0;
    bool dark_ = false;
    SyntaxLanguage language_ = SyntaxLanguage::Plain;
    std::wstring languageName_ = L"Plain text";
    SciteProperties properties_;
};

} // namespace editmdview
