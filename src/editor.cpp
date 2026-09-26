#include "editor.hpp"
#include "i18n.hpp"
#include <ILexer.h>
#include <LexerModule.h>
#include <SciLexer.h>
#include <Scintilla.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern "C" int Scintilla_RegisterClasses(void* hInstance);

extern const Lexilla::LexerModule lmBash;
extern const Lexilla::LexerModule lmBatch;
extern const Lexilla::LexerModule lmCmake;
extern const Lexilla::LexerModule lmConf;
extern const Lexilla::LexerModule lmCPP;
extern const Lexilla::LexerModule lmCss;
extern const Lexilla::LexerModule lmHTML;
extern const Lexilla::LexerModule lmJSON;
extern const Lexilla::LexerModule lmLISP;
extern const Lexilla::LexerModule lmLua;
extern const Lexilla::LexerModule lmMake;
extern const Lexilla::LexerModule lmMarkdown;
extern const Lexilla::LexerModule lmPowerShell;
extern const Lexilla::LexerModule lmProps;
extern const Lexilla::LexerModule lmPython;
extern const Lexilla::LexerModule lmRust;
extern const Lexilla::LexerModule lmSQL;
extern const Lexilla::LexerModule lmXML;
extern const Lexilla::LexerModule lmYAML;

namespace editmdview {
namespace {

std::once_flag g_scintillaRegistrationOnce;
bool g_scintillaRegistered = false;

bool ensure_scintilla_registered(HINSTANCE instance) {
    std::call_once(g_scintillaRegistrationOnce, [instance] {
        // Scintilla's static API requires registration once per loaded plugin
        // module. DLL detach releases the classes so a later load can register
        // them again in the same Total Commander process.
        g_scintillaRegistered = Scintilla_RegisterClasses(instance) != 0;
    });
    return g_scintillaRegistered;
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool is_one_of(std::wstring_view value, std::initializer_list<std::wstring_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

SyntaxLanguage language_for_path(const std::filesystem::path& path) {
    const std::wstring extension = lower(path.extension().wstring());
    const std::wstring filename = lower(path.filename().wstring());
    if (is_one_of(extension, {L".md", L".markdown", L".mkd", L".mkdn"})) return SyntaxLanguage::Markdown;
    if (is_one_of(extension, {L".c", L".cc", L".cpp", L".cxx", L".h", L".hh", L".hpp", L".hxx", L".inl", L".cs", L".java", L".go"})) return SyntaxLanguage::Cpp;
    if (is_one_of(extension, {L".js", L".jsx", L".mjs", L".cjs", L".ts", L".tsx"})) return SyntaxLanguage::JavaScript;
    if (is_one_of(extension, {L".py", L".pyw"})) return SyntaxLanguage::Python;
    if (is_one_of(extension, {L".json", L".jsonc"})) return SyntaxLanguage::Json;
    if (is_one_of(extension, {L".html", L".htm", L".xhtml", L".shtml", L".php"})) return SyntaxLanguage::Html;
    if (is_one_of(extension, {L".xml", L".svg", L".xaml", L".plist"})) return SyntaxLanguage::Xml;
    if (is_one_of(extension, {L".css", L".scss", L".less"})) return SyntaxLanguage::Css;
    if (is_one_of(extension, {L".sh", L".bash", L".zsh", L".fish"})) return SyntaxLanguage::Bash;
    if (is_one_of(extension, {L".sql"})) return SyntaxLanguage::Sql;
    if (is_one_of(extension, {L".yaml", L".yml"})) return SyntaxLanguage::Yaml;
    if (is_one_of(extension, {L".ini", L".cfg", L".properties", L".toml"})) return SyntaxLanguage::Properties;
    if (extension == L".conf" || filename == L".htaccess") return SyntaxLanguage::Conf;
    if (extension == L".cmake" || filename == L"cmakelists.txt") return SyntaxLanguage::CMake;
    if (is_one_of(extension, {L".mk", L".mak"}) || filename == L"makefile") return SyntaxLanguage::Makefile;
    if (is_one_of(extension, {L".bat", L".cmd"})) return SyntaxLanguage::Batch;
    if (is_one_of(extension, {L".ps1", L".psm1", L".psd1"})) return SyntaxLanguage::PowerShell;
    if (extension == L".rs") return SyntaxLanguage::Rust;
    if (extension == L".lua") return SyntaxLanguage::Lua;
    if (is_one_of(extension, {L".lisp", L".lsp", L".cl", L".el", L".scm", L".ss", L".rkt"})) return SyntaxLanguage::Lisp;
    return SyntaxLanguage::Plain;
}

std::optional<SyntaxLanguage> language_for_lexer_name(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (value == "null" || value == "text" || value == "container") return SyntaxLanguage::Plain;
    if (value == "markdown") return SyntaxLanguage::Markdown;
    if (value == "cpp" || value == "c" || value == "java") return SyntaxLanguage::Cpp;
    if (value == "javascript" || value == "typescript") return SyntaxLanguage::JavaScript;
    if (value == "python") return SyntaxLanguage::Python;
    if (value == "json") return SyntaxLanguage::Json;
    if (value == "hypertext" || value == "html") return SyntaxLanguage::Html;
    if (value == "xml") return SyntaxLanguage::Xml;
    if (value == "css") return SyntaxLanguage::Css;
    if (value == "bash" || value == "shell") return SyntaxLanguage::Bash;
    if (value == "sql") return SyntaxLanguage::Sql;
    if (value == "yaml") return SyntaxLanguage::Yaml;
    if (value == "props" || value == "properties") return SyntaxLanguage::Properties;
    if (value == "conf") return SyntaxLanguage::Conf;
    if (value == "cmake") return SyntaxLanguage::CMake;
    if (value == "makefile" || value == "make") return SyntaxLanguage::Makefile;
    if (value == "batch") return SyntaxLanguage::Batch;
    if (value == "powershell") return SyntaxLanguage::PowerShell;
    if (value == "rust") return SyntaxLanguage::Rust;
    if (value == "lua") return SyntaxLanguage::Lua;
    if (value == "lisp" || value == "scheme") return SyntaxLanguage::Lisp;
    return std::nullopt;
}

const char* style_lexer_name(SyntaxLanguage language) {
    switch (language) {
    case SyntaxLanguage::Markdown: return "markdown";
    case SyntaxLanguage::Cpp:
    case SyntaxLanguage::JavaScript: return "cpp";
    case SyntaxLanguage::Python: return "python";
    case SyntaxLanguage::Json: return "json";
    case SyntaxLanguage::Html: return "hypertext";
    case SyntaxLanguage::Xml: return "xml";
    case SyntaxLanguage::Css: return "css";
    case SyntaxLanguage::Bash: return "bash";
    case SyntaxLanguage::Sql: return "sql";
    case SyntaxLanguage::Yaml: return "yaml";
    case SyntaxLanguage::Properties: return "props";
    case SyntaxLanguage::Conf: return "conf";
    case SyntaxLanguage::CMake: return "cmake";
    case SyntaxLanguage::Makefile: return "makefile";
    case SyntaxLanguage::Batch: return "batch";
    case SyntaxLanguage::PowerShell: return "powershell";
    case SyntaxLanguage::Rust: return "rust";
    case SyntaxLanguage::Lua: return "lua";
    case SyntaxLanguage::Lisp: return "lisp";
    case SyntaxLanguage::Plain: return "text";
    }
    return "text";
}

std::optional<COLORREF> parse_colour(std::string_view value) {
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#') return std::nullopt;
    char* end = nullptr;
    const std::string digits(value.substr(1, 6));
    const unsigned long parsed = std::strtoul(digits.c_str(), &end, 16);
    if (end != digits.c_str() + digits.size()) return std::nullopt;
    return RGB((parsed >> 16) & 0xff, (parsed >> 8) & 0xff, parsed & 0xff);
}

struct ConfiguredColour {
    COLORREF rgb{};
    unsigned alpha = 0xff;
};

std::optional<ConfiguredColour> parse_colour_alpha(std::string_view value) {
    const auto rgb = parse_colour(value);
    if (!rgb) return std::nullopt;
    unsigned alpha = 0xff;
    if (value.size() == 9) {
        const std::string digits(value.substr(7, 2));
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(digits.c_str(), &end, 16);
        if (end != digits.c_str() + digits.size()) return std::nullopt;
        alpha = static_cast<unsigned>(parsed);
    }
    return ConfiguredColour{*rgb, alpha};
}

LPARAM colour_alpha(ConfiguredColour value) {
    return static_cast<LPARAM>(static_cast<unsigned>(value.rgb) | (value.alpha << 24));
}

std::string_view trim_ascii(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

COLORREF color(unsigned red, unsigned green, unsigned blue) {
    return RGB(red, green, blue);
}

std::size_t position_after_save_cleanup(std::string_view original, std::size_t oldPosition,
    bool stripTrailing, bool consistentEnds, std::string_view configuredEol) {
    oldPosition = std::min(oldPosition, original.size());
    std::size_t outputPosition = 0;
    std::size_t lineStart = 0;
    std::size_t cursor = 0;
    while (cursor < original.size()) {
        if (original[cursor] != '\r' && original[cursor] != '\n') {
            ++cursor;
            continue;
        }

        std::size_t lineEnd = cursor;
        if (stripTrailing) {
            while (lineEnd > lineStart &&
                (original[lineEnd - 1] == ' ' || original[lineEnd - 1] == '\t')) {
                --lineEnd;
            }
        }
        if (oldPosition <= lineEnd) return outputPosition + oldPosition - lineStart;
        outputPosition += lineEnd - lineStart;

        // Any position in whitespace removed from the end of this line maps
        // to the new physical line end.
        if (oldPosition <= cursor) return outputPosition;

        std::size_t terminatorEnd = cursor + 1;
        if (original[cursor] == '\r' && terminatorEnd < original.size() &&
            original[terminatorEnd] == '\n') {
            ++terminatorEnd;
        }
        const std::size_t newTerminatorLength = consistentEnds
            ? configuredEol.size() : terminatorEnd - cursor;
        if (oldPosition <= terminatorEnd) {
            return outputPosition + std::min(oldPosition - cursor, newTerminatorLength);
        }
        outputPosition += newTerminatorLength;
        cursor = terminatorEnd;
        lineStart = cursor;
    }

    std::size_t lineEnd = original.size();
    if (stripTrailing) {
        while (lineEnd > lineStart &&
            (original[lineEnd - 1] == ' ' || original[lineEnd - 1] == '\t')) {
            --lineEnd;
        }
    }
    if (oldPosition <= lineEnd) return outputPosition + oldPosition - lineStart;
    return outputPosition + lineEnd - lineStart;
}

} // namespace

bool Editor::create(HWND parent, HINSTANCE instance, bool dark, std::wstring& error) {
    if (!ensure_scintilla_registered(instance)) {
        error = i18n::text(L"Unable to register the Scintilla window class.");
        return false;
    }

    window_ = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
        0, 0, 100, 100, parent, nullptr, instance, nullptr);
    if (!window_) {
        error = i18n::text(L"Unable to create the Scintilla editor.");
        return false;
    }

    dark_ = dark;
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    apply_rendering_settings();
    send(SCI_SETMARGINWIDTHN, 0, 42);
    send(SCI_SETMARGINWIDTHN, 1, 0);
    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETUSETABS, FALSE);
    send(SCI_SETINDENT, 4);
    send(SCI_SETWRAPMODE, wrapEnabled_ ? SC_WRAP_WORD : SC_WRAP_NONE);
    send(SCI_SETCARETLINEVISIBLEALWAYS, TRUE);
    send(SCI_SETCARETLINEFRAME, 1);
    send(SCI_SETMULTIPLESELECTION, TRUE);
    send(SCI_SETADDITIONALSELECTIONTYPING, TRUE);
    send(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_ENABLED);
    apply_styles(dark);
    apply_editor_settings();
    return true;
}

void Editor::destroy() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Editor::resize(const RECT& bounds) const {
    if (!window_) return;
    MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, TRUE);
}

void Editor::show(bool visible) const {
    if (window_) ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

void Editor::focus() const {
    if (window_) SetFocus(window_);
}

void Editor::set_properties(SciteProperties properties) {
    properties_ = std::move(properties);
}

void Editor::reload_configuration(const std::filesystem::path& path, bool dark) {
    dark_ = dark;
    configure_lexer(path);
    apply_rendering_settings();
    apply_styles(dark_);
    apply_editor_settings();
    send(SCI_COLOURISE, 0, -1);
    update_ui();
}

std::uint64_t Editor::configuration_signature() const {
    return properties_.configuration_signature();
}

void Editor::set_text(std::string_view utf8Text, const std::filesystem::path& path, std::wstring_view eolName) {
    send(SCI_SETREADONLY, FALSE);
    send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(""));

    configure_lexer(path);
    apply_rendering_settings();
    apply_styles(dark_);
    const std::string contents(utf8Text);
    send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(contents.c_str()));

    std::wstring selectedEol(eolName);
    if (!properties_.boolean_for_file("eol.auto", true)) {
        if (const auto configuredEol = properties_.value_for_file("eol.mode")) {
            const std::string normalized = lower(*configuredEol);
            if (normalized == "crlf") selectedEol = L"CRLF";
            else if (normalized == "cr") selectedEol = L"CR";
            else if (normalized == "lf") selectedEol = L"LF";
        }
    }
    if (selectedEol == L"CRLF") send(SCI_SETEOLMODE, SC_EOL_CRLF);
    else if (selectedEol == L"CR") send(SCI_SETEOLMODE, SC_EOL_CR);
    else send(SCI_SETEOLMODE, SC_EOL_LF);

    apply_editor_settings();
    send(SCI_COLOURISE, 0, -1);
    if (properties_.boolean_for_file("fold.on.open", false)) {
        send(SCI_FOLDALL, SC_FOLDACTION_CONTRACT);
    }
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
}

void Editor::restore_unsaved_text(std::string_view utf8Text) {
    send(SCI_SETREADONLY, FALSE);
    send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(utf8Text.data()));
}

void Editor::set_language(SyntaxLanguage language) {
    if (language == language_) return;
    // A manually chosen lexer must not inherit the original extension's
    // language-specific keyword list (for example C++ keywords in Lisp).
    configure_language(language, false);
    apply_styles(dark_);
    send(SCI_COLOURISE, 0, -1);
}

void Editor::convert_eol(int scintillaEolMode) {
    if (scintillaEolMode != SC_EOL_CRLF && scintillaEolMode != SC_EOL_CR &&
        scintillaEolMode != SC_EOL_LF) return;
    send(SCI_BEGINUNDOACTION);
    send(SCI_CONVERTEOLS, static_cast<WPARAM>(scintillaEolMode));
    send(SCI_SETEOLMODE, static_cast<WPARAM>(scintillaEolMode));
    send(SCI_ENDUNDOACTION);
}

std::string Editor::text() const {
    const auto length = static_cast<std::size_t>(send(SCI_GETLENGTH));
    std::string result(length + 1, '\0');
    send(SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(result.data()));
    result.resize(length);
    return result;
}

std::string Editor::text_for_save() {
    const std::string original = text();
    const bool stripTrailing = properties_.boolean_for_file("strip.trailing.spaces", false);
    const bool consistentEnds = properties_.boolean_for_file("ensure.consistent.line.ends", false);
    const bool finalEnd = properties_.boolean_for_file("ensure.final.line.end", false);
    if (!stripTrailing && !consistentEnds && !finalEnd) return original;

    const int eolMode = static_cast<int>(send(SCI_GETEOLMODE));
    const std::string configuredEol = eolMode == SC_EOL_CRLF ? "\r\n" : (eolMode == SC_EOL_CR ? "\r" : "\n");
    std::string result;
    result.reserve(original.size() + configuredEol.size());
    std::size_t lineStart = 0;
    std::size_t cursor = 0;
    while (cursor < original.size()) {
        if (original[cursor] != '\r' && original[cursor] != '\n') {
            ++cursor;
            continue;
        }
        std::size_t lineEnd = cursor;
        if (stripTrailing) {
            while (lineEnd > lineStart && (original[lineEnd - 1] == ' ' || original[lineEnd - 1] == '\t')) --lineEnd;
        }
        result.append(original, lineStart, lineEnd - lineStart);
        std::size_t terminatorEnd = cursor + 1;
        if (original[cursor] == '\r' && terminatorEnd < original.size() && original[terminatorEnd] == '\n') {
            ++terminatorEnd;
        }
        if (consistentEnds) result += configuredEol;
        else result.append(original, cursor, terminatorEnd - cursor);
        cursor = terminatorEnd;
        lineStart = cursor;
    }
    std::size_t lineEnd = original.size();
    if (stripTrailing) {
        while (lineEnd > lineStart && (original[lineEnd - 1] == ' ' || original[lineEnd - 1] == '\t')) --lineEnd;
    }
    result.append(original, lineStart, lineEnd - lineStart);
    if (finalEnd && !result.empty() && result.back() != '\r' && result.back() != '\n') result += configuredEol;
    if (result == original) return result;

    const auto oldAnchor = static_cast<std::size_t>(send(SCI_GETANCHOR));
    const auto oldCaret = static_cast<std::size_t>(send(SCI_GETCURRENTPOS));
    const auto newAnchor = static_cast<LRESULT>(position_after_save_cleanup(original, oldAnchor,
        stripTrailing, consistentEnds, configuredEol));
    const auto newCaret = static_cast<LRESULT>(position_after_save_cleanup(original, oldCaret,
        stripTrailing, consistentEnds, configuredEol));
    const LRESULT firstVisibleLine = send(SCI_GETFIRSTVISIBLELINE);
    const LRESULT firstVisibleDocumentLine = send(SCI_DOCLINEFROMVISIBLE,
        static_cast<WPARAM>(firstVisibleLine));
    const LRESULT firstDisplayLineForDocument = send(SCI_VISIBLEFROMDOCLINE,
        static_cast<WPARAM>(firstVisibleDocumentLine));
    const LRESULT wrappedLineOffset = std::max<LRESULT>(0,
        firstVisibleLine - firstDisplayLineForDocument);
    const LRESULT horizontalOffset = send(SCI_GETXOFFSET);
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, 0);
    send(SCI_SETTARGETEND, static_cast<WPARAM>(original.size()));
    send(SCI_REPLACETARGET, result.size(), reinterpret_cast<LPARAM>(result.data()));
    send(SCI_ENDUNDOACTION);
    send(SCI_SETSEL, static_cast<WPARAM>(newAnchor), newCaret);
    send(SCI_SCROLLCARET);
    const LRESULT newFirstDisplayLine = send(SCI_VISIBLEFROMDOCLINE,
        static_cast<WPARAM>(firstVisibleDocumentLine));
    const LRESULT newWrapCount = std::max<LRESULT>(1, send(SCI_WRAPCOUNT,
        static_cast<WPARAM>(firstVisibleDocumentLine)));
    const LRESULT restoredFirstVisibleLine = newFirstDisplayLine +
        std::min(wrappedLineOffset, newWrapCount - 1);
    send(SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(restoredFirstVisibleLine));
    send(SCI_SETXOFFSET, static_cast<WPARAM>(horizontalOffset));
    return result;
}

bool Editor::modified() const {
    return send(SCI_GETMODIFY) != 0;
}

void Editor::mark_saved() const {
    send(SCI_SETSAVEPOINT);
}

void Editor::set_dark(bool dark) {
    dark_ = dark;
    apply_rendering_settings();
    apply_styles(dark);
    apply_editor_settings();
    send(SCI_COLOURISE, 0, -1);
}

void Editor::cut() const { send(SCI_CUT); }
void Editor::copy() const { send(SCI_COPY); }
void Editor::paste() const { send(SCI_PASTE); }
void Editor::delete_selection() const { send(SCI_CLEAR); }
void Editor::select_all() const { send(SCI_SELECTALL); }
void Editor::undo() const { send(SCI_UNDO); }
void Editor::redo() const { send(SCI_REDO); }
bool Editor::can_undo() const { return send(SCI_CANUNDO) != 0; }
bool Editor::can_redo() const { return send(SCI_CANREDO) != 0; }
bool Editor::has_selection() const { return send(SCI_GETSELECTIONSTART) != send(SCI_GETSELECTIONEND); }
std::string Editor::selected_text() const {
    const LRESULT length = send(SCI_GETSELTEXT);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length + 1), '\0');
    send(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(result.data()));
    result.resize(static_cast<std::size_t>(length));
    return result;
}
bool Editor::can_paste() const { return send(SCI_CANPASTE) != 0; }

bool Editor::markdown_shortcuts_enabled() const {
    return markdown_ && properties_.boolean_for_file("markdown.edit.shortcuts", true);
}

bool Editor::markdown_slash_enabled() const {
    return markdown_ && properties_.boolean_for_file("markdown.slash.commands", true);
}

bool Editor::apply_markdown_format(MarkdownFormat format) {
    if (!markdown_shortcuts_enabled()) return false;

    std::string opening;
    std::string closing;
    switch (format) {
    case MarkdownFormat::Bold: opening = closing = "**"; break;
    case MarkdownFormat::Italic: opening = closing = "*"; break;
    case MarkdownFormat::Highlight: opening = closing = "=="; break;
    case MarkdownFormat::Strikethrough: opening = closing = "~~"; break;
    case MarkdownFormat::InlineCode: opening = closing = "`"; break;
    }

    struct Selection {
        int index = 0;
        Sci_Position start = 0;
        Sci_Position end = 0;
    };
    const int selectionCount = std::max(1, static_cast<int>(send(SCI_GETSELECTIONS)));
    std::vector<Selection> selections;
    selections.reserve(static_cast<std::size_t>(selectionCount));
    for (int index = 0; index < selectionCount; ++index) {
        selections.push_back({index,
            static_cast<Sci_Position>(send(SCI_GETSELECTIONNSTART, index)),
            static_cast<Sci_Position>(send(SCI_GETSELECTIONNEND, index))});
    }
    std::sort(selections.begin(), selections.end(), [](const Selection& left, const Selection& right) {
        return left.start > right.start;
    });

    auto range_text = [&](Sci_Position start, Sci_Position end) {
        if (start < 0 || end < start || end > static_cast<Sci_Position>(send(SCI_GETLENGTH))) {
            return std::string{};
        }
        const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER,
            static_cast<WPARAM>(start), static_cast<LPARAM>(end - start)));
        return bytes ? std::string(bytes, static_cast<std::size_t>(end - start)) : std::string{};
    };
    auto replace = [&](Sci_Position start, Sci_Position end, std::string_view value) {
        send(SCI_SETTARGETSTART, static_cast<WPARAM>(start));
        send(SCI_SETTARGETEND, static_cast<WPARAM>(end));
        send(SCI_REPLACETARGET, value.size(), reinterpret_cast<LPARAM>(value.data()));
    };
    auto set_selection = [&](int index, Sci_Position start, Sci_Position end) {
        send(SCI_SETSELECTIONNANCHOR, index, start);
        send(SCI_SETSELECTIONNCARET, index, end);
    };

    send(SCI_BEGINUNDOACTION);
    for (const Selection& selection : selections) {
        const Sci_Position start = selection.start;
        const Sci_Position end = selection.end;
        const Sci_Position openingLength = static_cast<Sci_Position>(opening.size());
        const Sci_Position closingLength = static_cast<Sci_Position>(closing.size());
        if (start != end) {
            const std::string selected = range_text(start, end);
            bool includesMarkers = selected.size() >= opening.size() + closing.size() &&
                selected.starts_with(opening) && selected.ends_with(closing);
            bool surrounded = start >= openingLength &&
                range_text(start - openingLength, start) == opening &&
                range_text(end, end + closingLength) == closing;
            // A single '*' must not consume one half of a surrounding '**'.
            // Odd runs represent italic; even runs represent bold-only text.
            if (format == MarkdownFormat::Italic) {
                std::size_t prefixStars = 0;
                while (prefixStars < selected.size() && selected[prefixStars] == '*') ++prefixStars;
                std::size_t suffixStars = 0;
                while (suffixStars < selected.size() &&
                    selected[selected.size() - suffixStars - 1] == '*') ++suffixStars;
                includesMarkers = includesMarkers && prefixStars % 2 == 1 && suffixStars % 2 == 1;
                Sci_Position leftStars = 0;
                while (start - leftStars - 1 >= 0 &&
                    range_text(start - leftStars - 1, start - leftStars) == "*") ++leftStars;
                Sci_Position rightStars = 0;
                const Sci_Position documentLength = static_cast<Sci_Position>(send(SCI_GETLENGTH));
                while (end + rightStars < documentLength &&
                    range_text(end + rightStars, end + rightStars + 1) == "*") ++rightStars;
                surrounded = surrounded && leftStars % 2 == 1 && rightStars % 2 == 1;
            }
            if (includesMarkers) {
                const std::string inner = selected.substr(opening.size(),
                    selected.size() - opening.size() - closing.size());
                replace(start, end, inner);
                set_selection(selection.index, start,
                    start + static_cast<Sci_Position>(inner.size()));
            } else if (surrounded) {
                replace(end, end + closingLength, {});
                replace(start - openingLength, start, {});
                set_selection(selection.index, start - openingLength, end - openingLength);
            } else {
                const std::string replacement = opening + selected + closing;
                replace(start, end, replacement);
                set_selection(selection.index, start + openingLength,
                    start + openingLength + static_cast<Sci_Position>(selected.size()));
            }
        } else {
            bool emptyPair = start >= openingLength &&
                range_text(start - openingLength, start) == opening &&
                range_text(start, start + closingLength) == closing;
            if (format == MarkdownFormat::Italic && emptyPair) {
                Sci_Position leftStars = 0;
                while (start - leftStars - 1 >= 0 &&
                    range_text(start - leftStars - 1, start - leftStars) == "*") ++leftStars;
                Sci_Position rightStars = 0;
                const Sci_Position documentLength = static_cast<Sci_Position>(send(SCI_GETLENGTH));
                while (start + rightStars < documentLength &&
                    range_text(start + rightStars, start + rightStars + 1) == "*") ++rightStars;
                emptyPair = leftStars % 2 == 1 && rightStars % 2 == 1;
            }
            if (emptyPair) {
                replace(start, start + closingLength, {});
                replace(start - openingLength, start, {});
                set_selection(selection.index, start - openingLength, start - openingLength);
            } else {
                const std::string replacement = opening + closing;
                replace(start, start, replacement);
                set_selection(selection.index, start + openingLength, start + openingLength);
            }
        }
    }
    send(SCI_ENDUNDOACTION);
    send(SCI_SCROLLCARET);
    return true;
}

bool Editor::insert_markdown_link() {
    if (!markdown_shortcuts_enabled()) return false;
    const Sci_Position start = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    const Sci_Position end = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));
    std::string selected;
    if (end > start) {
        const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, start, end - start));
        if (bytes) selected.assign(bytes, static_cast<std::size_t>(end - start));
    }
    const std::string label = selected.empty() ? i18n::text_utf8("link text") : selected;
    const std::string address = i18n::text_utf8("URL");
    const std::string replacement = "[" + label + "](" + address + ")";
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, start);
    send(SCI_SETTARGETEND, end);
    send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    send(SCI_ENDUNDOACTION);
    if (selected.empty()) {
        send(SCI_SETSEL, start + 1, start + 1 + static_cast<Sci_Position>(label.size()));
    } else {
        const Sci_Position addressStart = start + 3 + static_cast<Sci_Position>(label.size());
        send(SCI_SETSEL, addressStart, addressStart + static_cast<Sci_Position>(address.size()));
    }
    send(SCI_SCROLLCARET);
    return true;
}

bool Editor::handle_markdown_enter() {
    if (!markdown_ || !properties_.boolean_for_file("markdown.smart.editing", true) ||
        send(SCI_GETSELECTIONSTART) != send(SCI_GETSELECTIONEND)) return false;
    const Sci_Position caret = static_cast<Sci_Position>(send(SCI_GETCURRENTPOS));
    const Sci_Position line = static_cast<Sci_Position>(send(SCI_LINEFROMPOSITION, caret));
    const Sci_Position lineStart = static_cast<Sci_Position>(send(SCI_POSITIONFROMLINE, line));
    const Sci_Position lineEnd = static_cast<Sci_Position>(send(SCI_GETLINEENDPOSITION, line));
    if (caret < lineStart || caret > lineEnd) return false;
    const int style = caret > lineStart ? static_cast<int>(send(SCI_GETSTYLEAT, caret - 1)) : 0;
    if (style == SCE_MARKDOWN_CODE || style == SCE_MARKDOWN_CODE2 || style == SCE_MARKDOWN_CODEBK) {
        return false;
    }
    auto range = [&](Sci_Position start, Sci_Position end) {
        const auto* bytes = reinterpret_cast<const char*>(send(
            SCI_GETRANGEPOINTER, static_cast<WPARAM>(start), end - start));
        return bytes ? std::string(bytes, static_cast<std::size_t>(end - start)) : std::string{};
    };
    const std::string prefix = range(lineStart, caret);
    const std::string suffix = range(caret, lineEnd);
    std::size_t cursor = 0;
    while (cursor < prefix.size() && (prefix[cursor] == ' ' || prefix[cursor] == '\t')) ++cursor;
    const std::string indentation = prefix.substr(0, cursor);
    const std::size_t quoteStart = cursor;
    while (cursor < prefix.size() && prefix[cursor] == '>') {
        ++cursor;
        if (cursor < prefix.size() && prefix[cursor] == ' ') ++cursor;
    }
    const std::string quotePrefix = prefix.substr(quoteStart, cursor - quoteStart);

    enum class Marker { None, Unordered, Ordered, Task, Quote } marker = Marker::None;
    std::string continuation;
    if (cursor + 1 < prefix.size() &&
        (prefix[cursor] == '-' || prefix[cursor] == '+' || prefix[cursor] == '*') &&
        prefix[cursor + 1] == ' ') {
        marker = Marker::Unordered;
        continuation.assign(prefix, cursor, 2);
        cursor += 2;
        if (cursor + 3 < prefix.size() && prefix[cursor] == '[' &&
            (prefix[cursor + 1] == ' ' || prefix[cursor + 1] == 'x' || prefix[cursor + 1] == 'X') &&
            prefix[cursor + 2] == ']' && prefix[cursor + 3] == ' ') {
            marker = Marker::Task;
            continuation += "[ ] ";
            cursor += 4;
        }
    } else {
        const std::size_t numberStart = cursor;
        while (cursor < prefix.size() && std::isdigit(static_cast<unsigned char>(prefix[cursor]))) ++cursor;
        if (cursor > numberStart && cursor + 1 < prefix.size() &&
            (prefix[cursor] == '.' || prefix[cursor] == ')') && prefix[cursor + 1] == ' ') {
            marker = Marker::Ordered;
            unsigned long long number = 0;
            const auto parsed = std::from_chars(prefix.data() + numberStart, prefix.data() + cursor, number);
            if (parsed.ec != std::errc{}) number = 0;
            number = std::min<unsigned long long>(number + 1, 999999ULL);
            continuation = std::to_string(number) + prefix[cursor] + " ";
            cursor += 2;
        } else {
            cursor = numberStart;
        }
    }
    if (marker == Marker::None && !quotePrefix.empty()) marker = Marker::Quote;
    if (marker == Marker::None) return false;

    const auto contains_text = [](std::string_view value) {
        return std::any_of(value.begin(), value.end(), [](unsigned char character) {
            return character != ' ' && character != '\t';
        });
    };
    const bool emptyItem = !contains_text(std::string_view(prefix).substr(cursor)) &&
        !contains_text(suffix);
    send(SCI_BEGINUNDOACTION);
    if (emptyItem) {
        std::string replacement;
        if (marker == Marker::Quote) {
            replacement = indentation + quotePrefix;
            const std::size_t finalQuote = replacement.rfind('>');
            if (finalQuote != std::string::npos) replacement.erase(finalQuote);
        } else {
            replacement = indentation + quotePrefix;
        }
        while (!replacement.empty() && replacement.back() == ' ' &&
            (replacement.size() < 2 || replacement[replacement.size() - 2] != '>')) {
            replacement.pop_back();
        }
        send(SCI_SETTARGETSTART, static_cast<WPARAM>(lineStart));
        send(SCI_SETTARGETEND, static_cast<WPARAM>(caret));
        send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
        const Sci_Position next = lineStart + static_cast<Sci_Position>(replacement.size());
        send(SCI_SETSEL, next, next);
    } else {
        std::string eol = "\n";
        if (send(SCI_GETEOLMODE) == SC_EOL_CRLF) eol = "\r\n";
        else if (send(SCI_GETEOLMODE) == SC_EOL_CR) eol = "\r";
        const std::string insertion = eol + indentation + quotePrefix + continuation;
        send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(insertion.c_str()));
    }
    send(SCI_ENDUNDOACTION);
    send(SCI_SCROLLCARET);
    return true;
}

bool Editor::handle_markdown_tab(bool backwards) {
    if (!markdown_ || !properties_.boolean_for_file("markdown.smart.editing", true)) return false;
    Sci_Position selectionStart = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    Sci_Position selectionEnd = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));
    const Sci_Position startLine = static_cast<Sci_Position>(send(SCI_LINEFROMPOSITION, selectionStart));
    Sci_Position endLine = static_cast<Sci_Position>(send(SCI_LINEFROMPOSITION, selectionEnd));
    if (selectionEnd > selectionStart && selectionEnd == send(SCI_POSITIONFROMLINE, endLine)) --endLine;
    auto line_text = [&](Sci_Position line) {
        const Sci_Position start = static_cast<Sci_Position>(send(SCI_POSITIONFROMLINE, line));
        const Sci_Position end = static_cast<Sci_Position>(send(SCI_GETLINEENDPOSITION, line));
        const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, start, end - start));
        return bytes ? std::string(bytes, static_cast<std::size_t>(end - start)) : std::string{};
    };
    bool hasItem = false;
    for (Sci_Position line = startLine; line <= endLine; ++line) {
        const std::string value = line_text(line);
        std::size_t cursor = 0;
        while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
        if (cursor == value.size()) continue;
        bool item = value[cursor] == '>' ||
            ((value[cursor] == '-' || value[cursor] == '+' || value[cursor] == '*') &&
                cursor + 1 < value.size() && value[cursor + 1] == ' ');
        if (!item && std::isdigit(static_cast<unsigned char>(value[cursor]))) {
            while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor]))) ++cursor;
            item = cursor + 1 < value.size() && (value[cursor] == '.' || value[cursor] == ')') &&
                value[cursor + 1] == ' ';
        }
        if (!item) return false;
        hasItem = true;
    }
    if (!hasItem) return false;

    const int indentWidth = properties_.integer_for_file("indent.size",
        properties_.integer_for_file("tabsize", 4, 1, 32), 1, 32);
    const bool useTabs = properties_.boolean_for_file("use.tabs", false);
    bool changed = false;
    send(SCI_BEGINUNDOACTION);
    for (Sci_Position line = endLine; line >= startLine; --line) {
        const Sci_Position start = static_cast<Sci_Position>(send(SCI_POSITIONFROMLINE, line));
        const std::string value = line_text(line);
        std::size_t removeLength = 0;
        std::string replacement;
        if (backwards) {
            if (!value.empty() && value.front() == '\t') removeLength = 1;
            else while (removeLength < value.size() && removeLength < static_cast<std::size_t>(indentWidth) &&
                value[removeLength] == ' ') ++removeLength;
            if (removeLength == 0) continue;
        } else {
            replacement = useTabs ? "\t" : std::string(static_cast<std::size_t>(indentWidth), ' ');
        }
        send(SCI_SETTARGETSTART, static_cast<WPARAM>(start));
        send(SCI_SETTARGETEND, static_cast<WPARAM>(start + static_cast<Sci_Position>(removeLength)));
        send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
        changed = true;
        if (line == 0) break;
    }
    send(SCI_ENDUNDOACTION);
    if (changed) send(SCI_SCROLLCARET);
    return changed;
}

bool Editor::handle_markdown_character(int character) {
    if (!markdown_ || !properties_.boolean_for_file("markdown.smart.editing", true) || character < 0x20) {
        return false;
    }
    const Sci_Position start = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    const Sci_Position end = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));
    char opening = static_cast<char>(character);
    char closing = '\0';
    switch (opening) {
    case '(': closing = ')'; break;
    case '[': closing = ']'; break;
    case '{': closing = '}'; break;
    case '"': closing = '"'; break;
    case '\'': closing = '\''; break;
    case '`': closing = '`'; break;
    case '*': case '_': case '~': closing = opening; break;
    case ')': case ']': case '}':
        if (start == end && start < send(SCI_GETLENGTH) && send(SCI_GETCHARAT, start) == opening) {
            send(SCI_SETSEL, start + 1, start + 1);
            return true;
        }
        return false;
    default: return false;
    }
    if (start == end && (opening == '*' || opening == '_' || opening == '~')) return false;
    if (start == end && send(SCI_GETCHARAT, start) == opening &&
        (opening == '"' || opening == '\'' || opening == '`')) {
        send(SCI_SETSEL, start + 1, start + 1);
        return true;
    }
    const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, start, end - start));
    const std::string selected = bytes && end > start
        ? std::string(bytes, static_cast<std::size_t>(end - start)) : std::string{};
    std::string replacement;
    replacement += opening;
    replacement += selected;
    replacement += closing;
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, static_cast<WPARAM>(start));
    send(SCI_SETTARGETEND, static_cast<WPARAM>(end));
    send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    send(SCI_ENDUNDOACTION);
    if (end > start) send(SCI_SETSEL, start + 1, start + 1 + static_cast<Sci_Position>(selected.size()));
    else send(SCI_SETSEL, start + 1, start + 1);
    return true;
}

bool Editor::paste_markdown_smart() {
    if (!markdown_ || !properties_.boolean_for_file("markdown.smart.editing", true) || !has_selection() ||
        !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(window_)) return false;
    std::wstring clipboard;
    if (HANDLE memory = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(memory))) {
            clipboard = text;
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    if (clipboard.empty() || clipboard.size() > 8192 ||
        clipboard.find_first_of(L"\r\n\t ") != std::wstring::npos ||
        !(clipboard.starts_with(L"http://") || clipboard.starts_with(L"https://") ||
            clipboard.starts_with(L"mailto:") || clipboard.starts_with(L"ftp://"))) return false;
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, clipboard.data(),
        static_cast<int>(clipboard.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return false;
    std::string address(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, clipboard.data(),
        static_cast<int>(clipboard.size()), address.data(), required, nullptr, nullptr);
    std::string label = selected_text();
    if (label.empty() || label.find_first_of("\r\n") != std::string::npos) return false;
    std::string escapedLabel;
    escapedLabel.reserve(label.size());
    for (const char byte : label) {
        if (byte == '\\' || byte == ']') escapedLabel += '\\';
        escapedLabel += byte;
    }
    const std::string replacement = "[" + escapedLabel + "](" + address + ")";
    const Sci_Position start = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    const Sci_Position end = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, static_cast<WPARAM>(start));
    send(SCI_SETTARGETEND, static_cast<WPARAM>(end));
    send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    send(SCI_ENDUNDOACTION);
    const Sci_Position caret = start + static_cast<Sci_Position>(replacement.size());
    send(SCI_SETSEL, caret, caret);
    send(SCI_SCROLLCARET);
    return true;
}

bool Editor::markdown_slash_context(std::intptr_t& startValue, std::intptr_t& endValue,
    std::string& query) const {
    if (!markdown_slash_enabled() || send(SCI_GETSELECTIONSTART) != send(SCI_GETSELECTIONEND)) return false;
    const Sci_Position caret = static_cast<Sci_Position>(send(SCI_GETCURRENTPOS));
    const Sci_Position line = static_cast<Sci_Position>(send(SCI_LINEFROMPOSITION, caret));
    const Sci_Position lineStart = static_cast<Sci_Position>(send(SCI_POSITIONFROMLINE, line));
    const Sci_Position lineEnd = static_cast<Sci_Position>(send(SCI_GETLINEENDPOSITION, line));
    if (caret < lineStart || caret > lineEnd) return false;
    const int style = caret > lineStart ? static_cast<int>(send(SCI_GETSTYLEAT, caret - 1)) : 0;
    if (style == SCE_MARKDOWN_CODE || style == SCE_MARKDOWN_CODE2 || style == SCE_MARKDOWN_CODEBK) return false;
    const auto* prefixBytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER,
        lineStart, caret - lineStart));
    const auto* suffixBytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER,
        caret, lineEnd - caret));
    if (!prefixBytes || (!suffixBytes && lineEnd > caret)) return false;
    const std::string prefix(prefixBytes, static_cast<std::size_t>(caret - lineStart));
    const std::string suffix = suffixBytes
        ? std::string(suffixBytes, static_cast<std::size_t>(lineEnd - caret)) : std::string{};
    if (std::any_of(suffix.begin(), suffix.end(), [](unsigned char character) {
            return character != ' ' && character != '\t';
        })) return false;
    std::size_t slash = 0;
    while (slash < prefix.size() && (prefix[slash] == ' ' || prefix[slash] == '\t')) ++slash;
    if (slash >= prefix.size() || prefix[slash] != '/') return false;
    for (std::size_t index = slash + 1; index < prefix.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(prefix[index]);
        if (character < 0x80 && !(std::isalnum(character) || character == '-' || character == '_')) return false;
    }
    startValue = lineStart + static_cast<Sci_Position>(slash);
    endValue = caret;
    query.assign(prefix.begin() + static_cast<std::ptrdiff_t>(slash + 1), prefix.end());
    return true;
}

bool Editor::insert_markdown_command(std::string_view command, int tableRows, int tableColumns) {
    std::intptr_t startValue = 0;
    std::intptr_t endValue = 0;
    std::string query;
    if (!markdown_slash_context(startValue, endValue, query)) return false;
    const Sci_Position start = static_cast<Sci_Position>(startValue);
    const Sci_Position end = static_cast<Sci_Position>(endValue);
    std::string replacement;
    std::size_t selectionOffset = 0;
    std::size_t selectionLength = 0;
    if (command == "h1") replacement = "# ";
    else if (command == "h2") replacement = "## ";
    else if (command == "h3") replacement = "### ";
    else if (command == "l1") replacement = "- ";
    else if (command == "l2") replacement = "1. ";
    else if (command == "l3") replacement = "- [ ] ";
    else if (command == "quote") replacement = "> ";
    else if (command == "code") {
        replacement = "```text\n\n```";
        selectionOffset = 3;
        selectionLength = 4;
    } else if (command == "v1") replacement = "> [!note]\n> ";
    else if (command == "v2") replacement = "> [!important]\n> ";
    else if (command == "v3") replacement = "> [!tip]\n> ";
    else if (command == "v4") replacement = "> [!warning]\n> ";
    else if (command == "v5") replacement = "> [!caution]\n> ";
    else if (command == "link") {
        replacement = "[" + i18n::text_utf8("link text") + "](" + i18n::text_utf8("URL") + ")";
        selectionOffset = 1;
        selectionLength = i18n::text_utf8("link text").size();
    } else if (command == "table") {
        tableRows = std::clamp(tableRows, 2, 20);
        tableColumns = std::clamp(tableColumns, 1, 20);
        std::string eol = "\n";
        if (send(SCI_GETEOLMODE) == SC_EOL_CRLF) eol = "\r\n";
        else if (send(SCI_GETEOLMODE) == SC_EOL_CR) eol = "\r";
        auto append_row = [&](std::string_view prefix, bool numbered) {
            replacement += "|";
            for (int column = 1; column <= tableColumns; ++column) {
                replacement += " ";
                replacement += prefix;
                if (numbered) replacement += std::to_string(column);
                replacement += " |";
            }
            replacement += eol;
        };
        append_row(i18n::text_utf8("Heading") + " ", true);
        append_row("---", false);
        for (int row = 1; row < tableRows; ++row) append_row("", false);
        selectionOffset = 2;
        selectionLength = (i18n::text_utf8("Heading") + " 1").size();
    } else {
        return false;
    }

    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETSTART, start);
    send(SCI_SETTARGETEND, end);
    send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    send(SCI_ENDUNDOACTION);
    if (selectionLength > 0) {
        send(SCI_SETSEL, start + static_cast<Sci_Position>(selectionOffset),
            start + static_cast<Sci_Position>(selectionOffset + selectionLength));
    } else {
        const Sci_Position caret = start + static_cast<Sci_Position>(replacement.size());
        send(SCI_SETSEL, caret, caret);
    }
    send(SCI_SCROLLCARET);
    return true;
}

void Editor::cancel_auto_completion() const {
    if (send(SCI_AUTOCACTIVE)) send(SCI_AUTOCCANCEL);
}

POINT Editor::caret_screen_point() const {
    const LRESULT position = send(SCI_GETCURRENTPOS);
    POINT point{
        static_cast<LONG>(send(SCI_POINTXFROMPOSITION, 0, position)),
        static_cast<LONG>(send(SCI_POINTYFROMPOSITION, 0, position) +
            send(SCI_TEXTHEIGHT, send(SCI_LINEFROMPOSITION, position)))
    };
    if (window_) ClientToScreen(window_, &point);
    return point;
}

void Editor::toggle_wrap() {
    if (!window_) return;
    const EditorViewState view = view_state();
    RECT client{};
    GetClientRect(window_, &client);
    const int caretY = static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, view.caret));
    const int caretLine = static_cast<int>(send(SCI_LINEFROMPOSITION, view.caret));
    const int lineHeight = std::max(1, static_cast<int>(send(SCI_TEXTHEIGHT, caretLine)));
    const bool caretVisible = caretY >= 0 && caretY < client.bottom;
    const int topDocumentLine = std::max(0, static_cast<int>(send(
        SCI_DOCLINEFROMVISIBLE, static_cast<WPARAM>(view.firstVisibleLine))));
    const int topDocumentFirstVisible = std::max(0, static_cast<int>(send(
        SCI_VISIBLEFROMDOCLINE, static_cast<WPARAM>(topDocumentLine))));
    const int topWrappedOffset = std::max(0, view.firstVisibleLine - topDocumentFirstVisible);

    if (!wrapEnabled_) unwrappedHorizontalOffset_ = view.horizontalOffset;
    wrapEnabled_ = !wrapEnabled_;
    send(SCI_SETWRAPMODE, wrapEnabled_ ? SC_WRAP_WORD : SC_WRAP_NONE);
    send(SCI_SETSEL, static_cast<WPARAM>(view.anchor), view.caret);

    if (caretVisible) {
        // Wrapping changes display-line numbering. Keep the caret on the same
        // screen row instead of restoring the old display-line index.
        for (int attempt = 0; attempt < 2; ++attempt) {
            const int currentY = static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, view.caret));
            const int lineDelta = (currentY - caretY) / lineHeight;
            if (lineDelta == 0) break;
            send(SCI_LINESCROLL, 0, lineDelta);
        }
    } else {
        int newFirstVisible = std::max(0, static_cast<int>(send(
            SCI_VISIBLEFROMDOCLINE, static_cast<WPARAM>(topDocumentLine))));
        if (wrapEnabled_) {
            const int wrapCount = std::max(1, static_cast<int>(send(
                SCI_WRAPCOUNT, static_cast<WPARAM>(topDocumentLine))));
            newFirstVisible += std::min(topWrappedOffset, wrapCount - 1);
        }
        send(SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(newFirstVisible));
    }
    send(SCI_SETXOFFSET, static_cast<WPARAM>(
        wrapEnabled_ ? 0 : std::max(0, unwrappedHorizontalOffset_)));
}

void Editor::zoom_in() const { send(SCI_ZOOMIN); }
void Editor::zoom_out() const { send(SCI_ZOOMOUT); }
void Editor::reset_zoom() const { send(SCI_SETZOOM, 0); }
int Editor::zoom() const { return static_cast<int>(send(SCI_GETZOOM)); }

void Editor::go_to_line(int oneBasedLine) const {
    const int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    const int line = std::clamp(oneBasedLine, 1, std::max(1, lineCount));
    send(SCI_GOTOLINE, static_cast<WPARAM>(line - 1));
    send(SCI_SCROLLCARET);
}

void Editor::toggle_fold_at(std::intptr_t position) const {
    if (position < 0) return;
    const LRESULT line = send(SCI_LINEFROMPOSITION, static_cast<WPARAM>(position));
    if (line >= 0) send(SCI_TOGGLEFOLD, static_cast<WPARAM>(line));
}

void Editor::update_ui() const {
    const bool highlightGuides = properties_.boolean_for_file("highlight.indentation.guides", false);
    send(SCI_SETHIGHLIGHTGUIDE, 0);

    if (!properties_.boolean_for_file("braces.check", false)) {
        send(SCI_BRACEHIGHLIGHT, static_cast<WPARAM>(-1), -1);
        return;
    }
    const LRESULT caret = send(SCI_GETCURRENTPOS);
    auto is_brace = [&](LRESULT position) {
        if (position < 0 || position >= send(SCI_GETLENGTH)) return false;
        const int character = static_cast<int>(send(SCI_GETCHARAT, position));
        return character == '(' || character == ')' || character == '[' || character == ']' ||
            character == '{' || character == '}';
    };
    LRESULT brace = is_brace(caret - 1) ? caret - 1 : -1;
    if (brace < 0 && properties_.boolean_for_file("braces.sloppy", false) && is_brace(caret)) brace = caret;
    if (brace < 0) {
        send(SCI_BRACEHIGHLIGHT, static_cast<WPARAM>(-1), -1);
        return;
    }
    std::string braceStyleKey = std::string("braces.") + style_lexer_name(language_) + ".style";
    auto configuredStyle = properties_.value_for_file(braceStyleKey);
    if (!configuredStyle && (language_ == SyntaxLanguage::Html || language_ == SyntaxLanguage::Xml)) {
        configuredStyle = properties_.value_for_file("braces.xml.style");
    }
    const int expected = configuredStyle
        ? static_cast<int>(std::strtol(configuredStyle->c_str(), nullptr, 10)) : 0;
    if (send(SCI_GETSTYLEAT, brace) != expected) {
        send(SCI_BRACEHIGHLIGHT, static_cast<WPARAM>(-1), -1);
        return;
    }
    const LRESULT match = send(SCI_BRACEMATCH, brace, 0);
    if (match >= 0) {
        send(SCI_BRACEHIGHLIGHT, brace, match);
        if (highlightGuides) {
            const LRESULT braceLine = send(SCI_LINEFROMPOSITION, brace);
            const LRESULT matchLine = send(SCI_LINEFROMPOSITION, match);
            const LRESULT braceIndent = send(SCI_GETLINEINDENTATION, braceLine);
            const LRESULT matchIndent = send(SCI_GETLINEINDENTATION, matchLine);
            send(SCI_SETHIGHLIGHTGUIDE, std::min(braceIndent, matchIndent));
        }
    } else {
        send(SCI_BRACEBADLIGHT, brace);
    }
}

void Editor::character_added(int character) {
    apply_auto_indent(character);
    if (character == '>') auto_close_xml_tag();
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_') {
        show_word_completion();
    }
}

void Editor::apply_auto_indent(int character) {
    if (!properties_.boolean_for_file("indent.automatic", false) &&
        !properties_.boolean_for_file("indent.auto", false)) return;
    const int indentWidth = properties_.integer_for_file("indent.size",
        properties_.integer_for_file("tabsize", 4, 1, 32), 0, 32);
    const LRESULT position = send(SCI_GETCURRENTPOS);
    const LRESULT line = send(SCI_LINEFROMPOSITION, position);
    if (character == '\n' || character == '\r') {
        if (line <= 0) return;
        const LRESULT previousLine = line - 1;
        int indentation = static_cast<int>(send(SCI_GETLINEINDENTATION, previousLine));
        if (properties_.boolean_for_file("indent.opening", false)) {
            const LRESULT previousStart = send(SCI_POSITIONFROMLINE, previousLine);
            const LRESULT previousEnd = send(SCI_GETLINEENDPOSITION, previousLine);
            const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, previousStart,
                previousEnd - previousStart));
            if (bytes) {
                std::size_t length = static_cast<std::size_t>(previousEnd - previousStart);
                while (length > 0 && (bytes[length - 1] == ' ' || bytes[length - 1] == '\t')) --length;
                if (length > 0 && (bytes[length - 1] == '{' || bytes[length - 1] == '[' ||
                    bytes[length - 1] == '(' || bytes[length - 1] == ':')) indentation += indentWidth;
            }
        }
        send(SCI_SETLINEINDENTATION, line, std::max(0, indentation));
        send(SCI_GOTOPOS, send(SCI_GETLINEINDENTPOSITION, line));
        return;
    }
    if ((character == '}' || character == ']' || character == ')') &&
        properties_.boolean_for_file("indent.closing", false)) {
        const LRESULT lineStart = send(SCI_POSITIONFROMLINE, line);
        const LRESULT beforeCharacter = send(SCI_POSITIONBEFORE, position);
        const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, lineStart,
            beforeCharacter - lineStart));
        bool onlyIndent = true;
        if (bytes) {
            for (LRESULT index = 0; index < beforeCharacter - lineStart; ++index) {
                if (bytes[index] != ' ' && bytes[index] != '\t') { onlyIndent = false; break; }
            }
        }
        if (onlyIndent) {
            const int indentation = static_cast<int>(send(SCI_GETLINEINDENTATION, line));
            send(SCI_SETLINEINDENTATION, line, std::max(0, indentation - indentWidth));
            send(SCI_GOTOPOS, send(SCI_GETLINEENDPOSITION, line));
        }
    }
}

void Editor::show_word_completion() {
    // SciTE deliberately treats this property as an exact switch: only 1
    // enables document-word completion. Existing configurations commonly use
    // another value (for example 2) to keep the setting present but disabled.
    if (properties_.value_for_file("autocompleteword.automatic") !=
        std::optional<std::string>("1")) return;
    const LRESULT position = send(SCI_GETCURRENTPOS);
    const LRESULT start = send(SCI_WORDSTARTPOSITION, position, TRUE);
    const LRESULT prefixLength = position - start;
    if (prefixLength < 1 || prefixLength > 128) return;
    const auto* prefixBytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, start, prefixLength));
    if (!prefixBytes) return;
    const std::string prefix(prefixBytes, static_cast<std::size_t>(prefixLength));
    const bool ignoreCase = properties_.boolean_for_file("autocomplete.ignorecase", false);
    const std::string comparisonPrefix = ignoreCase ? lower(prefix) : prefix;
    std::set<std::string> candidates;
    auto consider = [&](std::string word) {
        const std::string comparison = ignoreCase ? lower(word) : word;
        if (word != prefix && comparison.rfind(comparisonPrefix, 0) == 0) candidates.insert(std::move(word));
    };

    const std::string document = text();
    if (document.size() <= 2'000'000) {
        std::size_t cursor = 0;
        while (cursor < document.size() && candidates.size() < 300) {
            while (cursor < document.size() && !(std::isalnum(static_cast<unsigned char>(document[cursor])) ||
                document[cursor] == '_')) ++cursor;
            const std::size_t wordStart = cursor;
            while (cursor < document.size() && (std::isalnum(static_cast<unsigned char>(document[cursor])) ||
                document[cursor] == '_')) ++cursor;
            if (cursor > wordStart) consider(document.substr(wordStart, cursor - wordStart));
        }
    }
    if (candidates.size() != 1) {
        if (send(SCI_AUTOCACTIVE)) send(SCI_AUTOCCANCEL);
        return;
    }
    std::string list;
    for (const std::string& candidate : candidates) {
        if (!list.empty()) list.push_back(' ');
        list += candidate;
    }
    send(SCI_AUTOCSETSEPARATOR, ' ');
    send(SCI_AUTOCSHOW, prefixLength, reinterpret_cast<LPARAM>(list.c_str()));
}

void Editor::auto_close_xml_tag() {
    if (!properties_.boolean_for_file("xml.auto.close.tags", false) ||
        (language_ != SyntaxLanguage::Html && language_ != SyntaxLanguage::Xml)) return;
    const LRESULT position = send(SCI_GETCURRENTPOS);
    const LRESULT scanStart = std::max<LRESULT>(0, position - 512);
    const auto* bytes = reinterpret_cast<const char*>(send(SCI_GETRANGEPOINTER, scanStart, position - scanStart));
    if (!bytes) return;
    const std::string fragment(bytes, static_cast<std::size_t>(position - scanStart));
    const std::size_t opening = fragment.rfind('<');
    if (opening == std::string::npos || opening + 1 >= fragment.size() || fragment[opening + 1] == '/' ||
        fragment[opening + 1] == '!' || fragment[opening + 1] == '?' ||
        (fragment.size() >= 2 && fragment[fragment.size() - 2] == '/')) return;
    std::size_t end = opening + 1;
    while (end < fragment.size() && (std::isalnum(static_cast<unsigned char>(fragment[end])) ||
        fragment[end] == ':' || fragment[end] == '-' || fragment[end] == '_')) ++end;
    if (end == opening + 1) return;
    const std::string tag = fragment.substr(opening + 1, end - opening - 1);
    if (language_ == SyntaxLanguage::Html) {
        static const std::set<std::string> voidElements = {"area", "base", "br", "col", "embed", "hr",
            "img", "input", "link", "meta", "param", "source", "track", "wbr"};
        if (voidElements.contains(lower(tag))) return;
    }
    const std::string close = "</" + tag + ">";
    send(SCI_INSERTTEXT, position, reinterpret_cast<LPARAM>(close.c_str()));
}

bool Editor::find(std::string_view utf8Needle, bool matchCase, bool wholeWord, bool backwards, bool fromStart) {
    if (utf8Needle.empty()) return false;
    const auto documentLength = static_cast<Sci_Position>(send(SCI_GETLENGTH));
    const auto selectionStart = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    const auto selectionEnd = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));

    int flags = SCFIND_NONE;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    send(SCI_SETSEARCHFLAGS, flags);

    auto searchRange = [&](Sci_Position start, Sci_Position end) -> Sci_Position {
        send(SCI_SETTARGETSTART, start);
        send(SCI_SETTARGETEND, end);
        return static_cast<Sci_Position>(send(SCI_SEARCHINTARGET, utf8Needle.size(),
            reinterpret_cast<LPARAM>(utf8Needle.data())));
    };

    Sci_Position found = -1;
    if (backwards) {
        found = searchRange(fromStart ? documentLength : selectionStart, 0);
        if (found < 0 && !fromStart) found = searchRange(documentLength, selectionStart);
    } else {
        found = searchRange(fromStart ? 0 : selectionEnd, documentLength);
        if (found < 0 && !fromStart) found = searchRange(0, selectionEnd);
    }
    if (found < 0) return false;

    const auto end = static_cast<Sci_Position>(send(SCI_GETTARGETEND));
    send(SCI_SETSEL, found, end);
    send(SCI_SCROLLCARET);
    return true;
}

bool Editor::replace_selection_if_match(std::string_view utf8Needle,
    std::string_view utf8Replacement, bool matchCase, bool wholeWord) {
    if (utf8Needle.empty()) return false;
    const auto selectionStart = static_cast<Sci_Position>(send(SCI_GETSELECTIONSTART));
    const auto selectionEnd = static_cast<Sci_Position>(send(SCI_GETSELECTIONEND));
    if (selectionStart == selectionEnd) return false;

    int flags = SCFIND_NONE;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    send(SCI_SETSEARCHFLAGS, flags);
    send(SCI_SETTARGETSTART, selectionStart);
    send(SCI_SETTARGETEND, selectionEnd);
    const auto found = static_cast<Sci_Position>(send(SCI_SEARCHINTARGET, utf8Needle.size(),
        reinterpret_cast<LPARAM>(utf8Needle.data())));
    if (found != selectionStart || static_cast<Sci_Position>(send(SCI_GETTARGETEND)) != selectionEnd) {
        return false;
    }
    send(SCI_REPLACETARGET, utf8Replacement.size(), reinterpret_cast<LPARAM>(utf8Replacement.data()));
    const auto replacementEnd = static_cast<Sci_Position>(send(SCI_GETTARGETEND));
    send(SCI_SETSEL, replacementEnd, replacementEnd);
    send(SCI_SCROLLCARET);
    return true;
}

int Editor::replace_all(std::string_view utf8Needle, std::string_view utf8Replacement,
    bool matchCase, bool wholeWord) {
    if (utf8Needle.empty()) return 0;
    int flags = SCFIND_NONE;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    send(SCI_SETSEARCHFLAGS, flags);
    send(SCI_BEGINUNDOACTION);
    int replacements = 0;
    Sci_Position searchStart = 0;
    while (searchStart <= static_cast<Sci_Position>(send(SCI_GETLENGTH))) {
        const auto documentEnd = static_cast<Sci_Position>(send(SCI_GETLENGTH));
        send(SCI_SETTARGETSTART, searchStart);
        send(SCI_SETTARGETEND, documentEnd);
        const auto found = static_cast<Sci_Position>(send(SCI_SEARCHINTARGET, utf8Needle.size(),
            reinterpret_cast<LPARAM>(utf8Needle.data())));
        if (found < 0) break;
        send(SCI_REPLACETARGET, utf8Replacement.size(), reinterpret_cast<LPARAM>(utf8Replacement.data()));
        searchStart = static_cast<Sci_Position>(send(SCI_GETTARGETEND));
        ++replacements;
    }
    send(SCI_ENDUNDOACTION);
    return replacements;
}

int Editor::current_line() const {
    const auto position = send(SCI_GETCURRENTPOS);
    return static_cast<int>(send(SCI_LINEFROMPOSITION, position)) + 1;
}

int Editor::current_column() const {
    const auto position = send(SCI_GETCURRENTPOS);
    return static_cast<int>(send(SCI_GETCOLUMN, position)) + 1;
}

int Editor::line_count() const {
    return std::max(1, static_cast<int>(send(SCI_GETLINECOUNT)));
}

int Editor::first_visible_document_line() const {
    const LRESULT visibleLine = send(SCI_GETFIRSTVISIBLELINE);
    return std::max(0, static_cast<int>(send(SCI_DOCLINEFROMVISIBLE, visibleLine)));
}

int Editor::visible_line_count() const {
    return std::max(1, static_cast<int>(send(SCI_LINESONSCREEN)));
}

EditorViewState Editor::view_state() const {
    EditorViewState state;
    state.anchor = send(SCI_GETANCHOR);
    state.caret = send(SCI_GETCURRENTPOS);
    state.firstVisibleLine = std::max(0, static_cast<int>(send(SCI_GETFIRSTVISIBLELINE)));
    const auto topDocumentLine = std::max<LRESULT>(0,
        send(SCI_DOCLINEFROMVISIBLE, static_cast<WPARAM>(state.firstVisibleLine)));
    const auto topDocumentStart = std::max<LRESULT>(0,
        send(SCI_POSITIONFROMLINE, static_cast<WPARAM>(topDocumentLine)));
    const int lineHeight = std::max(1, static_cast<int>(send(
        SCI_TEXTHEIGHT, static_cast<WPARAM>(topDocumentLine))));
    const int documentX = static_cast<int>(send(
        SCI_POINTXFROMPOSITION, 0, topDocumentStart));
    const int textLeft = documentX + std::max(0, static_cast<int>(send(SCI_GETXOFFSET))) + 1;
    const auto pointPosition = send(SCI_POSITIONFROMPOINTCLOSE,
        static_cast<WPARAM>(std::max(0, textLeft)), lineHeight / 2);
    state.topVisiblePosition = pointPosition >= 0 ? pointPosition : topDocumentStart;
    state.horizontalOffset = std::max(0, static_cast<int>(send(SCI_GETXOFFSET)));
    return state;
}

void Editor::restore_view_state(const EditorViewState& state) const {
    const auto length = std::max<LRESULT>(0, send(SCI_GETLENGTH));
    const auto anchor = std::clamp<LRESULT>(state.anchor, 0, length);
    const auto caret = std::clamp<LRESULT>(state.caret, 0, length);
    send(SCI_SETSEL, static_cast<WPARAM>(anchor), caret);
    if (state.topVisiblePosition >= 0) {
        const auto topPosition = std::clamp<LRESULT>(state.topVisiblePosition, 0, length);
        const auto topDocumentLine = std::max<LRESULT>(0, send(
            SCI_LINEFROMPOSITION, static_cast<WPARAM>(topPosition)));
        const auto firstDisplayLine = std::max<LRESULT>(0, send(
            SCI_VISIBLEFROMDOCLINE, static_cast<WPARAM>(topDocumentLine)));
        send(SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(firstDisplayLine));
        const int lineHeight = std::max(1, static_cast<int>(send(
            SCI_TEXTHEIGHT, static_cast<WPARAM>(topDocumentLine))));
        const int positionY = static_cast<int>(send(
            SCI_POINTYFROMPOSITION, 0, topPosition));
        if (positionY > 0) send(SCI_LINESCROLL, 0, positionY / lineHeight);
    } else {
        send(SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(std::max(0, state.firstVisibleLine)));
    }
    send(SCI_SETXOFFSET, static_cast<WPARAM>(std::max(0, state.horizontalOffset)));
}

std::wstring Editor::eol_name() const {
    switch (send(SCI_GETEOLMODE)) {
    case SC_EOL_CRLF: return L"CRLF";
    case SC_EOL_CR: return L"CR";
    default: return L"LF";
    }
}

LRESULT Editor::send(UINT message, WPARAM wParam, LPARAM lParam) const {
    return window_ ? SendMessageW(window_, message, wParam, lParam) : 0;
}

void Editor::configure_lexer(const std::filesystem::path& path) {
    SyntaxLanguage language = language_for_path(path);
    if (const auto configuredLexer = properties_.value_for_file("lexer")) {
        if (const auto configuredLanguage = language_for_lexer_name(*configuredLexer)) {
            language = *configuredLanguage;
        }
    }
    configure_language(language, true);
}

void Editor::configure_language(SyntaxLanguage language, bool useFileSpecificKeywords) {
    language_ = language;
    markdown_ = language_ == SyntaxLanguage::Markdown;

    const Lexilla::LexerModule* module = nullptr;
    const char* keywords = "";
    const char* secondaryKeywords = "";
    switch (language_) {
    case SyntaxLanguage::Markdown:
        module = &lmMarkdown; languageName_ = L"Markdown"; break;
    case SyntaxLanguage::Cpp:
        module = &lmCPP; languageName_ = L"C/C++/Java";
        keywords = "alignas alignof auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend if inline int long namespace new noexcept nullptr operator private protected public register reinterpret_cast return short signed sizeof static static_assert struct switch template this throw true try typedef typename union unsigned using virtual void volatile wchar_t while interface package import synchronized throws extends implements final abstract byte instanceof native strictfp transient var record sealed permits";
        secondaryKeywords = "std string vector map set unordered_map unique_ptr shared_ptr optional variant size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t";
        break;
    case SyntaxLanguage::JavaScript:
        module = &lmCPP; languageName_ = L"JavaScript/TypeScript";
        keywords = "async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield interface type enum implements namespace declare readonly private protected public abstract any boolean number string unknown never keyof infer satisfies";
        secondaryKeywords = "Array BigInt Boolean Date Error JSON Map Math Number Object Promise Proxy Reflect RegExp Set String Symbol WeakMap WeakSet console document globalThis window";
        break;
    case SyntaxLanguage::Python:
        module = &lmPython; languageName_ = L"Python";
        keywords = "and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield match case";
        secondaryKeywords = "bool bytearray bytes complex dict float frozenset int list memoryview object range set slice str tuple type zip Exception";
        break;
    case SyntaxLanguage::Json:
        module = &lmJSON; languageName_ = L"JSON";
        keywords = "true false null";
        break;
    case SyntaxLanguage::Html:
        module = &lmHTML; languageName_ = L"HTML";
        keywords = "html head body title meta link style script main header footer nav section article aside div span p a img picture source h1 h2 h3 h4 h5 h6 ul ol li table thead tbody tr th td form label input button select option textarea details summary dialog canvas video audio template slot";
        break;
    case SyntaxLanguage::Xml:
        module = &lmXML; languageName_ = L"XML"; break;
    case SyntaxLanguage::Css:
        module = &lmCss; languageName_ = L"CSS";
        keywords = "color background border display position margin padding width height min-width max-width min-height max-height font content flex grid gap align-items justify-content overflow opacity transform transition animation box-sizing z-index";
        break;
    case SyntaxLanguage::Bash:
        module = &lmBash; languageName_ = L"Shell";
        keywords = "if then else elif fi for while in do done case esac function select until time coproc declare local readonly export unset set shift source alias return break continue true false";
        break;
    case SyntaxLanguage::Sql:
        module = &lmSQL; languageName_ = L"SQL";
        keywords = "select from where join inner left right full outer on as insert into values update set delete create alter drop table view index database schema distinct group by having order asc desc limit offset union all case when then else end null is not and or exists between like primary foreign key references constraint default unique check begin commit rollback with recursive";
        break;
    case SyntaxLanguage::Yaml:
        module = &lmYAML; languageName_ = L"YAML"; break;
    case SyntaxLanguage::Properties:
        module = &lmProps; languageName_ = i18n::text(L"Properties"); break;
    case SyntaxLanguage::Conf:
        module = &lmConf; languageName_ = i18n::text(L"Apache config"); break;
    case SyntaxLanguage::CMake:
        module = &lmCmake; languageName_ = L"CMake";
        keywords = "add_executable add_library add_subdirectory cmake_minimum_required find_package include install message option project set target_compile_definitions target_compile_features target_compile_options target_include_directories target_link_libraries if elseif else endif foreach endforeach function endfunction macro endmacro while endwhile";
        break;
    case SyntaxLanguage::Makefile:
        module = &lmMake; languageName_ = L"Makefile"; break;
    case SyntaxLanguage::Batch:
        module = &lmBatch; languageName_ = L"Batch";
        keywords = "echo set setlocal endlocal if else for in do goto call exit shift exist defined errorlevel not cmd rem start pushd popd";
        break;
    case SyntaxLanguage::PowerShell:
        module = &lmPowerShell; languageName_ = L"PowerShell";
        keywords = "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function if in inlineScript parallel param process return sequence switch throw trap try until using var while workflow";
        break;
    case SyntaxLanguage::Rust:
        module = &lmRust; languageName_ = L"Rust";
        keywords = "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while abstract become box do final macro override priv typeof unsized virtual yield";
        break;
    case SyntaxLanguage::Lua:
        module = &lmLua; languageName_ = L"Lua";
        keywords = "and break do else elseif end false for function goto if in local nil not or repeat return then true until while";
        break;
    case SyntaxLanguage::Lisp:
        module = &lmLISP; languageName_ = L"Lisp/Scheme";
        keywords = "and begin case cond define define-syntax defmacro defun do else if lambda let let* letrec or quote set! when unless";
        break;
    case SyntaxLanguage::Plain:
    default:
        languageName_ = i18n::text(L"Plain text"); break;
    }

    std::array<std::string, 9> keywordSets;
    keywordSets[0] = keywords;
    keywordSets[1] = secondaryKeywords;
    for (std::size_t index = 0; index < keywordSets.size(); ++index) {
        const std::string propertyName = index == 0 ? "keywords" : "keywords" + std::to_string(index + 1);
        if (useFileSpecificKeywords) {
            if (const auto configuredKeywords = properties_.value_for_file(propertyName)) {
                keywordSets[index] = *configuredKeywords;
            }
        }
    }
    if (module) {
        Scintilla::ILexer5* lexer = module->Create();
        send(SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
        // SCI_SETILEXER takes ownership of the lexer reference.
        for (std::size_t index = 0; index < keywordSets.size(); ++index) {
            send(SCI_SETKEYWORDS, static_cast<WPARAM>(index),
                reinterpret_cast<LPARAM>(keywordSets[index].c_str()));
        }
        if (language_ == SyntaxLanguage::JavaScript) {
            send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("lexer.cpp.allow.dollars"), reinterpret_cast<LPARAM>("1"));
        }
        if (language_ == SyntaxLanguage::Json) {
            send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("lexer.json.allow.comments"), reinterpret_cast<LPARAM>("1"));
        }
        const int propertyNamesLength = static_cast<int>(send(SCI_PROPERTYNAMES));
        if (propertyNamesLength > 0) {
            std::string propertyNames(static_cast<std::size_t>(propertyNamesLength + 1), '\0');
            send(SCI_PROPERTYNAMES, propertyNames.size(), reinterpret_cast<LPARAM>(propertyNames.data()));
            std::size_t start = 0;
            while (start < propertyNames.size()) {
                const std::size_t end = propertyNames.find('\n', start);
                const std::string name = propertyNames.substr(start,
                    end == std::string::npos ? propertyNames.size() - start : end - start);
                if (!name.empty()) {
                    if (const auto configuredValue = properties_.value(name)) {
                        send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>(name.c_str()),
                            reinterpret_cast<LPARAM>(configuredValue->c_str()));
                    }
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        if (const auto fold = properties_.value_for_file("fold")) {
            send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("fold"), reinterpret_cast<LPARAM>(fold->c_str()));
        }
    } else {
        send(SCI_SETILEXER, 0, 0);
        for (std::size_t index = 0; index < keywordSets.size(); ++index) {
            send(SCI_SETKEYWORDS, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(""));
        }
    }
}

void Editor::apply_rendering_settings() {
    // These values affect font creation and must be applied before styles.
    send(SCI_SETTECHNOLOGY, static_cast<WPARAM>(
        properties_.integer_for_file("technology", SC_TECHNOLOGY_DEFAULT, SC_TECHNOLOGY_DEFAULT,
            SC_TECHNOLOGY_DIRECT_WRITE_1)));
    send(SCI_SETFONTQUALITY, static_cast<WPARAM>(
        properties_.integer_for_file("font.quality", SC_EFF_QUALITY_DEFAULT,
            SC_EFF_QUALITY_DEFAULT, SC_EFF_QUALITY_LCD_OPTIMIZED)));
    if (const auto locale = properties_.value_for_file("font.locale")) {
        send(SCI_SETFONTLOCALE, 0, reinterpret_cast<LPARAM>(locale->c_str()));
    } else {
        send(SCI_SETFONTLOCALE, 0, reinterpret_cast<LPARAM>("en-us"));
    }
    send(SCI_SETBUFFEREDDRAW, properties_.boolean_for_file("buffered.draw", true));
    send(SCI_SETPHASESDRAW, static_cast<WPARAM>(
        properties_.integer_for_file("phases.draw", SC_PHASES_TWO, SC_PHASES_ONE, SC_PHASES_MULTIPLE)));
}

void Editor::apply_styles(bool dark) {
    const COLORREF background = dark ? color(13, 17, 23) : color(255, 255, 255);
    const COLORREF foreground = dark ? color(230, 237, 243) : color(31, 35, 40);
    const COLORREF muted = dark ? color(139, 148, 158) : color(87, 96, 106);
    const COLORREF accent = dark ? color(88, 166, 255) : color(9, 105, 218);
    const COLORREF purple = dark ? color(210, 168, 255) : color(130, 80, 223);
    const COLORREF green = dark ? color(126, 231, 135) : color(26, 127, 55);
    const COLORREF red = dark ? color(255, 123, 114) : color(207, 34, 46);
    const COLORREF codeBackground = dark ? color(22, 27, 34) : color(246, 248, 250);

    auto apply_definition = [&](int style, std::string_view definition) {
        std::size_t start = 0;
        while (start <= definition.size()) {
            const std::size_t end = definition.find(',', start);
            const std::string_view token = trim_ascii(definition.substr(start,
                end == std::string_view::npos ? definition.size() - start : end - start));
            if (token.rfind("fore:", 0) == 0) {
                if (const auto configured = parse_colour(trim_ascii(token.substr(5)))) {
                    send(SCI_STYLESETFORE, style, *configured);
                }
            } else if (token.rfind("back:", 0) == 0) {
                if (const auto configured = parse_colour(trim_ascii(token.substr(5)))) {
                    send(SCI_STYLESETBACK, style, *configured);
                }
            } else if (token.rfind("font:", 0) == 0) {
                const std::string font(trim_ascii(token.substr(5)));
                if (!font.empty()) send(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(font.c_str()));
            } else if (token.rfind("size:", 0) == 0) {
                const std::string sizeText(trim_ascii(token.substr(5)));
                char* parseEnd = nullptr;
                const double size = std::strtod(sizeText.c_str(), &parseEnd);
                if (parseEnd != sizeText.c_str() && size >= 5.0 && size <= 200.0) {
                    send(SCI_STYLESETSIZEFRACTIONAL, style,
                        static_cast<LPARAM>(std::lround(size * SC_FONT_SIZE_MULTIPLIER)));
                }
            } else if (token.rfind("weight:", 0) == 0) {
                const std::string weightText(trim_ascii(token.substr(7)));
                const long weight = std::strtol(weightText.c_str(), nullptr, 10);
                if (weight >= 1 && weight <= 1000) send(SCI_STYLESETWEIGHT, style, weight);
            } else if (token.rfind("stretch:", 0) == 0) {
                const std::string stretchText(trim_ascii(token.substr(8)));
                const long stretch = std::strtol(stretchText.c_str(), nullptr, 10);
                if (stretch >= SC_STRETCH_ULTRA_CONDENSED && stretch <= SC_STRETCH_ULTRA_EXPANDED) {
                    send(SCI_STYLESETSTRETCH, style, stretch);
                }
            } else if (token.rfind("case:", 0) == 0) {
                const std::string caseText = lower(std::string(trim_ascii(token.substr(5))));
                int caseMode = SC_CASE_MIXED;
                if (caseText == "u" || caseText == "upper") caseMode = SC_CASE_UPPER;
                else if (caseText == "l" || caseText == "lower") caseMode = SC_CASE_LOWER;
                else if (caseText == "c" || caseText == "camel") caseMode = SC_CASE_CAMEL;
                send(SCI_STYLESETCASE, style, caseMode);
            } else if (token.rfind("charset:", 0) == 0) {
                const std::string charsetText(trim_ascii(token.substr(8)));
                const long charset = std::strtol(charsetText.c_str(), nullptr, 10);
                if (charset >= 0 && charset <= 1000) send(SCI_STYLESETCHARACTERSET, style, charset);
            } else if (token.rfind("invisiblerepresentation:", 0) == 0) {
                const std::string representation(trim_ascii(token.substr(24)));
                send(SCI_STYLESETINVISIBLEREPRESENTATION, style,
                    reinterpret_cast<LPARAM>(representation.c_str()));
            } else if (token == "bold") {
                send(SCI_STYLESETBOLD, style, TRUE);
            } else if (token == "notbold") {
                send(SCI_STYLESETBOLD, style, FALSE);
            } else if (token == "normal") {
                send(SCI_STYLESETWEIGHT, style, SC_WEIGHT_NORMAL);
                send(SCI_STYLESETITALIC, style, FALSE);
            } else if (token == "italics") {
                send(SCI_STYLESETITALIC, style, TRUE);
            } else if (token == "notitalics") {
                send(SCI_STYLESETITALIC, style, FALSE);
            } else if (token == "underlined") {
                send(SCI_STYLESETUNDERLINE, style, TRUE);
            } else if (token == "notunderlined") {
                send(SCI_STYLESETUNDERLINE, style, FALSE);
            } else if (token == "eolfilled") {
                send(SCI_STYLESETEOLFILLED, style, TRUE);
            } else if (token == "noteolfilled") {
                send(SCI_STYLESETEOLFILLED, style, FALSE);
            } else if (token == "visible") {
                send(SCI_STYLESETVISIBLE, style, TRUE);
            } else if (token == "notvisible") {
                send(SCI_STYLESETVISIBLE, style, FALSE);
            } else if (token == "changeable") {
                send(SCI_STYLESETCHANGEABLE, style, TRUE);
            } else if (token == "notchangeable") {
                send(SCI_STYLESETCHANGEABLE, style, FALSE);
            } else if (token == "hotspot") {
                send(SCI_STYLESETHOTSPOT, style, TRUE);
            } else if (token == "nothotspot") {
                send(SCI_STYLESETHOTSPOT, style, FALSE);
            }
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
    };

    send(SCI_STYLESETFORE, STYLE_DEFAULT, foreground);
    send(SCI_STYLESETBACK, STYLE_DEFAULT, background);
    send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Cascadia Mono"));
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    send(SCI_STYLESETWEIGHT, STYLE_DEFAULT, SC_WEIGHT_NORMAL);
    send(SCI_STYLESETSTRETCH, STYLE_DEFAULT, SC_STRETCH_NORMAL);
    send(SCI_STYLESETCASE, STYLE_DEFAULT, SC_CASE_MIXED);
    send(SCI_STYLESETITALIC, STYLE_DEFAULT, FALSE);
    send(SCI_STYLESETUNDERLINE, STYLE_DEFAULT, FALSE);
    send(SCI_STYLESETEOLFILLED, STYLE_DEFAULT, FALSE);
    send(SCI_STYLESETVISIBLE, STYLE_DEFAULT, TRUE);
    send(SCI_STYLESETCHANGEABLE, STYLE_DEFAULT, TRUE);
    send(SCI_STYLESETHOTSPOT, STYLE_DEFAULT, FALSE);
    if (const auto font = properties_.value("font.base")) apply_definition(STYLE_DEFAULT, *font);
    else if (const auto font = properties_.value("font.monospace")) apply_definition(STYLE_DEFAULT, *font);
    if (const auto configuredDefault = properties_.value("style.*.32")) {
        apply_definition(STYLE_DEFAULT, *configuredDefault);
    }
    const std::string lexerStylePrefix = std::string("style.") + style_lexer_name(language_) + ".";
    if (const auto configuredDefault = properties_.value(lexerStylePrefix + "32")) {
        apply_definition(STYLE_DEFAULT, *configuredDefault);
    }
    const bool configuredTypography = properties_.value("font.base").has_value() ||
        properties_.value("font.monospace").has_value() ||
        properties_.value("style.*.32").has_value() ||
        properties_.value(lexerStylePrefix + "32").has_value();
    send(SCI_STYLECLEARALL);
    send(SCI_SETCARETFORE, foreground);
    send(SCI_SETCARETLINEBACK, dark ? color(22, 27, 34) : color(246, 248, 250));
    send(SCI_SETSELFORE, TRUE, dark ? color(255, 255, 255) : color(31, 35, 40));
    send(SCI_SETSELBACK, TRUE, dark ? color(38, 79, 120) : color(173, 214, 255));

    send(SCI_STYLESETFORE, STYLE_LINENUMBER, muted);
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, dark ? color(13, 17, 23) : color(246, 248, 250));

    auto set_fore = [&](std::initializer_list<int> styles, COLORREF value) {
        for (int style : styles) send(SCI_STYLESETFORE, style, value);
    };
    const auto commentFont = properties_.value("font.comment");
    auto apply_comment_font = [&](std::initializer_list<int> styles) {
        if (!commentFont) return;
        for (int style : styles) apply_definition(style, *commentFont);
    };
    auto set_comments = [&](std::initializer_list<int> styles) {
        for (int style : styles) {
            send(SCI_STYLESETFORE, style, muted);
            send(SCI_STYLESETITALIC, style, TRUE);
        }
        apply_comment_font(styles);
    };
    auto set_keywords = [&](std::initializer_list<int> styles) {
        for (int style : styles) {
            send(SCI_STYLESETFORE, style, accent);
            send(SCI_STYLESETBOLD, style, TRUE);
        }
    };

    switch (language_) {
    case SyntaxLanguage::Markdown: {
        const std::array<int, 6> headings = {SCE_MARKDOWN_HEADER1, SCE_MARKDOWN_HEADER2,
            SCE_MARKDOWN_HEADER3, SCE_MARKDOWN_HEADER4, SCE_MARKDOWN_HEADER5, SCE_MARKDOWN_HEADER6};
        for (int style : headings) {
            send(SCI_STYLESETFORE, style, accent);
            send(SCI_STYLESETBOLD, style, TRUE);
        }
        if (!configuredTypography) {
            send(SCI_STYLESETSIZE, SCE_MARKDOWN_HEADER1, 17);
            send(SCI_STYLESETSIZE, SCE_MARKDOWN_HEADER2, 15);
            send(SCI_STYLESETSIZE, SCE_MARKDOWN_HEADER3, 13);
        }
        set_keywords({SCE_MARKDOWN_STRONG1, SCE_MARKDOWN_STRONG2});
        for (int style : {SCE_MARKDOWN_EM1, SCE_MARKDOWN_EM2}) {
            send(SCI_STYLESETFORE, style, purple);
            send(SCI_STYLESETITALIC, style, TRUE);
        }
        for (int style : {SCE_MARKDOWN_CODE, SCE_MARKDOWN_CODE2, SCE_MARKDOWN_CODEBK}) {
            send(SCI_STYLESETFORE, style, green);
            send(SCI_STYLESETBACK, style, codeBackground);
        }
        set_fore({SCE_MARKDOWN_BLOCKQUOTE, SCE_MARKDOWN_HRULE}, muted);
        set_fore({SCE_MARKDOWN_STRIKEOUT}, red);
        set_fore({SCE_MARKDOWN_ULIST_ITEM, SCE_MARKDOWN_OLIST_ITEM}, purple);
        send(SCI_STYLESETFORE, SCE_MARKDOWN_LINK, accent);
        send(SCI_STYLESETUNDERLINE, SCE_MARKDOWN_LINK, TRUE);
        break;
    }
    case SyntaxLanguage::Cpp:
    case SyntaxLanguage::JavaScript:
        set_comments({SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC, SCE_C_COMMENTLINEDOC,
            SCE_C_PREPROCESSORCOMMENT, SCE_C_PREPROCESSORCOMMENTDOC});
        apply_comment_font({SCE_C_COMMENTDOCKEYWORD, SCE_C_COMMENTDOCKEYWORDERROR,
            SCE_C_TASKMARKER});
        set_keywords({SCE_C_WORD, SCE_C_WORD2, SCE_C_GLOBALCLASS});
        set_fore({SCE_C_STRING, SCE_C_CHARACTER, SCE_C_VERBATIM, SCE_C_STRINGRAW,
            SCE_C_TRIPLEVERBATIM, SCE_C_HASHQUOTEDSTRING, SCE_C_REGEX}, green);
        set_fore({SCE_C_NUMBER, SCE_C_USERLITERAL}, purple);
        set_fore({SCE_C_PREPROCESSOR}, red);
        break;
    case SyntaxLanguage::Python:
        set_comments({SCE_P_COMMENTLINE, SCE_P_COMMENTBLOCK});
        set_keywords({SCE_P_WORD, SCE_P_WORD2, SCE_P_CLASSNAME, SCE_P_DEFNAME});
        set_fore({SCE_P_STRING, SCE_P_CHARACTER, SCE_P_TRIPLE, SCE_P_TRIPLEDOUBLE,
            SCE_P_FSTRING, SCE_P_FCHARACTER, SCE_P_FTRIPLE, SCE_P_FTRIPLEDOUBLE}, green);
        set_fore({SCE_P_NUMBER, SCE_P_DECORATOR}, purple);
        break;
    case SyntaxLanguage::Json:
        set_comments({SCE_JSON_LINECOMMENT, SCE_JSON_BLOCKCOMMENT});
        set_keywords({SCE_JSON_KEYWORD, SCE_JSON_LDKEYWORD});
        set_fore({SCE_JSON_STRING, SCE_JSON_PROPERTYNAME, SCE_JSON_URI, SCE_JSON_COMPACTIRI}, green);
        set_fore({SCE_JSON_NUMBER}, purple);
        set_fore({SCE_JSON_ERROR, SCE_JSON_STRINGEOL}, red);
        break;
    case SyntaxLanguage::Html:
    case SyntaxLanguage::Xml:
        set_comments({SCE_H_COMMENT, SCE_H_XCCOMMENT, SCE_H_SGML_COMMENT});
        apply_comment_font({SCE_H_SGML_1ST_PARAM_COMMENT,
            SCE_HJ_COMMENT, SCE_HJ_COMMENTLINE, SCE_HJ_COMMENTDOC,
            SCE_HJA_COMMENT, SCE_HJA_COMMENTLINE, SCE_HJA_COMMENTDOC,
            SCE_HB_COMMENTLINE, SCE_HBA_COMMENTLINE,
            SCE_HP_COMMENTLINE, SCE_HPA_COMMENTLINE,
            SCE_HPHP_COMMENT, SCE_HPHP_COMMENTLINE});
        set_keywords({SCE_H_TAG, SCE_H_SCRIPT, SCE_H_SGML_COMMAND});
        set_fore({SCE_H_ATTRIBUTE, SCE_H_ENTITY}, purple);
        set_fore({SCE_H_DOUBLESTRING, SCE_H_SINGLESTRING, SCE_H_VALUE, SCE_H_CDATA}, green);
        set_fore({SCE_H_TAGUNKNOWN, SCE_H_ATTRIBUTEUNKNOWN, SCE_H_SGML_ERROR}, red);
        break;
    case SyntaxLanguage::Css:
        set_comments({SCE_CSS_COMMENT});
        set_keywords({SCE_CSS_TAG, SCE_CSS_DIRECTIVE, SCE_CSS_GROUP_RULE});
        set_fore({SCE_CSS_CLASS, SCE_CSS_ID, SCE_CSS_PSEUDOCLASS, SCE_CSS_PSEUDOELEMENT}, purple);
        set_fore({SCE_CSS_DOUBLESTRING, SCE_CSS_SINGLESTRING, SCE_CSS_VALUE}, green);
        break;
    case SyntaxLanguage::Bash:
        set_comments({SCE_SH_COMMENTLINE});
        set_keywords({SCE_SH_WORD});
        set_fore({SCE_SH_STRING, SCE_SH_CHARACTER, SCE_SH_BACKTICKS, SCE_SH_HERE_Q}, green);
        set_fore({SCE_SH_NUMBER, SCE_SH_SCALAR, SCE_SH_PARAM}, purple);
        set_fore({SCE_SH_ERROR}, red);
        break;
    case SyntaxLanguage::Sql:
        set_comments({SCE_SQL_COMMENT, SCE_SQL_COMMENTLINE, SCE_SQL_COMMENTDOC, SCE_SQL_COMMENTLINEDOC});
        apply_comment_font({SCE_SQL_SQLPLUS_COMMENT, SCE_SQL_COMMENTDOCKEYWORD,
            SCE_SQL_COMMENTDOCKEYWORDERROR});
        set_keywords({SCE_SQL_WORD, SCE_SQL_WORD2});
        set_fore({SCE_SQL_STRING, SCE_SQL_CHARACTER, SCE_SQL_QUOTEDIDENTIFIER}, green);
        set_fore({SCE_SQL_NUMBER}, purple);
        break;
    case SyntaxLanguage::Yaml:
        set_comments({SCE_YAML_COMMENT});
        set_keywords({SCE_YAML_KEYWORD});
        set_fore({SCE_YAML_TEXT}, green);
        set_fore({SCE_YAML_NUMBER, SCE_YAML_REFERENCE, SCE_YAML_DOCUMENT}, purple);
        set_fore({SCE_YAML_ERROR}, red);
        break;
    case SyntaxLanguage::Properties:
        set_comments({SCE_PROPS_COMMENT});
        set_keywords({SCE_PROPS_SECTION});
        set_fore({SCE_PROPS_KEY}, purple);
        set_fore({SCE_PROPS_DEFVAL}, green);
        break;
    case SyntaxLanguage::Conf:
        set_comments({SCE_CONF_COMMENT});
        set_keywords({SCE_CONF_DIRECTIVE});
        set_fore({SCE_CONF_STRING, SCE_CONF_IP}, green);
        set_fore({SCE_CONF_NUMBER}, purple);
        break;
    case SyntaxLanguage::CMake:
        set_comments({SCE_CMAKE_COMMENT});
        set_keywords({SCE_CMAKE_COMMANDS, SCE_CMAKE_USERDEFINED, SCE_CMAKE_MACRODEF});
        set_fore({SCE_CMAKE_STRINGDQ, SCE_CMAKE_STRINGLQ, SCE_CMAKE_STRINGRQ, SCE_CMAKE_STRINGVAR}, green);
        set_fore({SCE_CMAKE_VARIABLE, SCE_CMAKE_PARAMETERS, SCE_CMAKE_NUMBER}, purple);
        break;
    case SyntaxLanguage::Makefile:
        set_comments({SCE_MAKE_COMMENT});
        set_keywords({SCE_MAKE_TARGET});
        set_fore({SCE_MAKE_PREPROCESSOR}, red);
        set_fore({SCE_MAKE_IDENTIFIER}, purple);
        break;
    case SyntaxLanguage::Batch:
        set_comments({SCE_BAT_COMMENT});
        set_keywords({SCE_BAT_WORD, SCE_BAT_COMMAND});
        set_fore({SCE_BAT_LABEL, SCE_BAT_IDENTIFIER}, purple);
        break;
    case SyntaxLanguage::PowerShell:
        set_comments({SCE_POWERSHELL_COMMENT, SCE_POWERSHELL_COMMENTSTREAM});
        apply_comment_font({SCE_POWERSHELL_COMMENTDOCKEYWORD});
        set_keywords({SCE_POWERSHELL_KEYWORD, SCE_POWERSHELL_CMDLET, SCE_POWERSHELL_ALIAS});
        set_fore({SCE_POWERSHELL_STRING, SCE_POWERSHELL_CHARACTER, SCE_POWERSHELL_HERE_STRING,
            SCE_POWERSHELL_HERE_CHARACTER}, green);
        set_fore({SCE_POWERSHELL_NUMBER, SCE_POWERSHELL_VARIABLE}, purple);
        break;
    case SyntaxLanguage::Rust:
        set_comments({SCE_RUST_COMMENTBLOCK, SCE_RUST_COMMENTLINE, SCE_RUST_COMMENTBLOCKDOC, SCE_RUST_COMMENTLINEDOC});
        set_keywords({SCE_RUST_WORD, SCE_RUST_WORD2, SCE_RUST_WORD3, SCE_RUST_WORD4});
        set_fore({SCE_RUST_STRING, SCE_RUST_STRINGR, SCE_RUST_CHARACTER, SCE_RUST_BYTESTRING,
            SCE_RUST_BYTESTRINGR, SCE_RUST_BYTECHARACTER, SCE_RUST_CSTRING, SCE_RUST_CSTRINGR}, green);
        set_fore({SCE_RUST_NUMBER, SCE_RUST_LIFETIME, SCE_RUST_MACRO}, purple);
        set_fore({SCE_RUST_LEXERROR}, red);
        break;
    case SyntaxLanguage::Lua:
        set_comments({SCE_LUA_COMMENT, SCE_LUA_COMMENTLINE, SCE_LUA_COMMENTDOC});
        set_keywords({SCE_LUA_WORD, SCE_LUA_WORD2});
        set_fore({SCE_LUA_STRING, SCE_LUA_CHARACTER, SCE_LUA_LITERALSTRING}, green);
        set_fore({SCE_LUA_NUMBER, SCE_LUA_LABEL}, purple);
        break;
    case SyntaxLanguage::Lisp:
        set_comments({SCE_LISP_COMMENT, SCE_LISP_MULTI_COMMENT});
        set_keywords({SCE_LISP_KEYWORD, SCE_LISP_KEYWORD_KW});
        set_fore({SCE_LISP_STRING, SCE_LISP_SPECIAL}, green);
        set_fore({SCE_LISP_NUMBER, SCE_LISP_SYMBOL}, purple);
        set_fore({SCE_LISP_STRINGEOL}, red);
        break;
    case SyntaxLanguage::Plain:
        break;
    }

    for (int style = 0; style <= STYLE_MAX; ++style) {
        if (style == STYLE_DEFAULT) continue;
        if (const auto generic = properties_.value("style.*." + std::to_string(style))) {
            apply_definition(style, *generic);
        }
        if (const auto specific = properties_.value(lexerStylePrefix + std::to_string(style))) {
            apply_definition(style, *specific);
        }
    }
    auto apply_element_property = [&](const char* name, int element, UINT legacyMessage = 0,
        WPARAM legacyEnabled = 0, bool legacyColourInWParam = false) {
        if (const auto configured = properties_.value_for_file(name)) {
            if (const auto parsed = parse_colour_alpha(trim_ascii(*configured))) {
                send(SCI_SETELEMENTCOLOUR, element, colour_alpha(*parsed));
                if (legacyMessage) {
                    if (legacyColourInWParam) send(legacyMessage, parsed->rgb);
                    else send(legacyMessage, legacyEnabled, parsed->rgb);
                }
            }
        }
    };
    apply_element_property("caret.fore", SC_ELEMENT_CARET, SCI_SETCARETFORE, 0, true);
    apply_element_property("caret.additional.fore", SC_ELEMENT_CARET_ADDITIONAL,
        SCI_SETADDITIONALCARETFORE, 0, true);
    apply_element_property("caret.line.back", SC_ELEMENT_CARET_LINE_BACK, SCI_SETCARETLINEBACK, 0, true);
    apply_element_property("selection.fore", SC_ELEMENT_SELECTION_TEXT, SCI_SETSELFORE, TRUE);
    apply_element_property("selection.back", SC_ELEMENT_SELECTION_BACK, SCI_SETSELBACK, TRUE);
    apply_element_property("selection.additional.fore", SC_ELEMENT_SELECTION_ADDITIONAL_TEXT,
        SCI_SETADDITIONALSELFORE, 0, true);
    apply_element_property("selection.additional.back", SC_ELEMENT_SELECTION_ADDITIONAL_BACK,
        SCI_SETADDITIONALSELBACK, 0, true);
    apply_element_property("selection.secondary.fore", SC_ELEMENT_SELECTION_SECONDARY_TEXT);
    apply_element_property("selection.secondary.back", SC_ELEMENT_SELECTION_SECONDARY_BACK);
    apply_element_property("selection.inactive.fore", SC_ELEMENT_SELECTION_INACTIVE_TEXT);
    apply_element_property("selection.inactive.back", SC_ELEMENT_SELECTION_INACTIVE_BACK);
}

void Editor::apply_editor_settings() {
    auto colour_property = [&](const char* name) -> std::optional<ConfiguredColour> {
        const auto configured = properties_.value_for_file(name);
        return configured ? parse_colour_alpha(trim_ascii(*configured)) : std::nullopt;
    };

    const int tabWidth = properties_.integer_for_file("tabsize", 4, 1, 32);
    const int configuredIndentWidth = properties_.integer_for_file("indent.size", tabWidth, 0, 32);
    int indentWidth = configuredIndentWidth;
    bool useTabs = properties_.boolean_for_file("use.tabs", false);
    if (properties_.boolean_for_file("indent.auto", false)) {
        const std::string contents = text();
        std::array<int, 33> spaceIndentCounts{};
        int tabIndentedLines = 0;
        int spaceIndentedLines = 0;
        std::size_t start = 0;
        int examinedLines = 0;
        while (start < contents.size() && examinedLines < 2000) {
            std::size_t end = contents.find_first_of("\r\n", start);
            if (end == std::string::npos) end = contents.size();
            std::size_t cursor = start;
            int spaces = 0;
            int tabs = 0;
            while (cursor < end && (contents[cursor] == ' ' || contents[cursor] == '\t')) {
                contents[cursor++] == '\t' ? ++tabs : ++spaces;
            }
            if (cursor < end) {
                if (tabs > 0) ++tabIndentedLines;
                else if (spaces > 0) {
                    ++spaceIndentedLines;
                    if (spaces <= 32) ++spaceIndentCounts[static_cast<std::size_t>(spaces)];
                }
                ++examinedLines;
            }
            if (end == contents.size()) break;
            start = end + ((contents[end] == '\r' && end + 1 < contents.size() && contents[end + 1] == '\n') ? 2 : 1);
        }
        useTabs = tabIndentedLines > spaceIndentedLines;
        if (!useTabs && spaceIndentedLines > 0) {
            int bestWidth = indentWidth;
            int bestScore = -1;
            for (int width = 2; width <= 8; ++width) {
                int score = 0;
                for (int spaces = width; spaces <= 32; spaces += width) score += spaceIndentCounts[spaces];
                if (score > bestScore || (score == bestScore && width == configuredIndentWidth)) {
                    bestScore = score;
                    bestWidth = width;
                }
            }
            indentWidth = bestWidth;
        }
    }
    send(SCI_SETTABWIDTH, static_cast<WPARAM>(tabWidth));
    send(SCI_SETINDENT, static_cast<WPARAM>(indentWidth));
    send(SCI_SETUSETABS, useTabs);
    send(SCI_SETTABINDENTS, properties_.boolean_for_file("tab.indents", true));
    send(SCI_SETBACKSPACEUNINDENTS, properties_.boolean_for_file("backspace.unindents", false));

    if (const auto wrap = properties_.value_for_file("wrap")) {
        const std::string normalized = lower(*wrap);
        wrapEnabled_ = !(normalized.empty() || normalized == "0" || normalized == "none");
    }
    int wrapMode = wrapEnabled_ ? SC_WRAP_WORD : SC_WRAP_NONE;
    if (const auto configured = properties_.value_for_file("wrap")) {
        const std::string normalized = lower(*configured);
        if (normalized == "char" || normalized == "2") wrapMode = SC_WRAP_CHAR;
        else if (normalized == "whitespace" || normalized == "3") wrapMode = SC_WRAP_WHITESPACE;
    }
    send(SCI_SETWRAPMODE, wrapMode);
    send(SCI_SETWRAPVISUALFLAGS, static_cast<WPARAM>(
        properties_.integer_for_file("wrap.visual.flags", SC_WRAPVISUALFLAG_NONE, 0, 7)));
    send(SCI_SETWRAPVISUALFLAGSLOCATION, static_cast<WPARAM>(
        properties_.integer_for_file("wrap.visual.flags.location", SC_WRAPVISUALFLAGLOC_DEFAULT, 0, 3)));
    send(SCI_SETWRAPINDENTMODE, static_cast<WPARAM>(
        properties_.integer_for_file("wrap.indent.mode", SC_WRAPINDENT_FIXED, SC_WRAPINDENT_FIXED,
            SC_WRAPINDENT_DEEPINDENT)));
    send(SCI_SETWRAPSTARTINDENT, static_cast<WPARAM>(
        properties_.integer_for_file("wrap.visual.startindent", 0, 0, 256)));

    const bool wrapAwareKeys = properties_.boolean_for_file("wrap.aware.home.end.keys", false);
    const WPARAM homeKey = static_cast<WPARAM>(SCK_HOME | (SCMOD_NORM << 16));
    const WPARAM shiftHomeKey = static_cast<WPARAM>(SCK_HOME | (SCMOD_SHIFT << 16));
    const WPARAM endKey = static_cast<WPARAM>(SCK_END | (SCMOD_NORM << 16));
    const WPARAM shiftEndKey = static_cast<WPARAM>(SCK_END | (SCMOD_SHIFT << 16));
    send(SCI_ASSIGNCMDKEY, homeKey, wrapAwareKeys ? SCI_VCHOMEWRAP : SCI_VCHOME);
    send(SCI_ASSIGNCMDKEY, shiftHomeKey, wrapAwareKeys ? SCI_VCHOMEWRAPEXTEND : SCI_VCHOMEEXTEND);
    send(SCI_ASSIGNCMDKEY, endKey, wrapAwareKeys ? SCI_LINEENDWRAP : SCI_LINEEND);
    send(SCI_ASSIGNCMDKEY, shiftEndKey, wrapAwareKeys ? SCI_LINEENDWRAPEXTEND : SCI_LINEENDEXTEND);

    int whitespaceMode = properties_.integer_for_file("view.whitespace", SCWS_INVISIBLE,
        SCWS_INVISIBLE, SCWS_VISIBLEONLYININDENT);
    if (whitespaceMode == SCWS_VISIBLEALWAYS) {
        const int indentationWhitespace = properties_.integer_for_file("view.indentation.whitespace", 1, 0, 2);
        if (indentationWhitespace == 0) whitespaceMode = SCWS_VISIBLEAFTERINDENT;
        else if (indentationWhitespace == 2) whitespaceMode = SCWS_VISIBLEONLYININDENT;
    }
    send(SCI_SETVIEWWS, whitespaceMode);
    const bool indentationGuides = properties_.boolean_for_file("view.indentation.guides", false);
    const int guideMode = indentationGuides
        ? properties_.integer_for_file("view.indentation.examine", SC_IV_REAL, SC_IV_REAL, SC_IV_LOOKBOTH)
        : SC_IV_NONE;
    send(SCI_SETINDENTATIONGUIDES, guideMode);
    send(SCI_SETVIEWEOL, properties_.boolean_for_file("view.eol", false));
    if (const auto symbol = properties_.value_for_file("control.char.symbol")) {
        send(SCI_SETCONTROLCHARSYMBOL, symbol->empty() ? 0 : static_cast<unsigned char>(symbol->front()));
    } else {
        send(SCI_SETCONTROLCHARSYMBOL, 0);
    }
    if (const auto whitespace = colour_property("whitespace.fore")) {
        send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE, colour_alpha(*whitespace));
        send(SCI_SETWHITESPACEFORE, TRUE, whitespace->rgb);
    }
    if (const auto whitespace = colour_property("whitespace.back")) {
        send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE_BACK, colour_alpha(*whitespace));
        send(SCI_SETWHITESPACEBACK, TRUE, whitespace->rgb);
    }
    send(SCI_SETWHITESPACESIZE, static_cast<WPARAM>(
        properties_.integer_for_file("whitespace.size", 1, 1, 64)));
    send(SCI_SETEXTRAASCENT, properties_.integer_for_file("extra.ascent", 0, -256, 256));
    send(SCI_SETEXTRADESCENT, properties_.integer_for_file("extra.descent", 0, -256, 256));
    send(SCI_SETMARGINLEFT, 0, properties_.integer_for_file("blank.margin.left", 0, 0, 1000));
    send(SCI_SETMARGINRIGHT, 0, properties_.integer_for_file("blank.margin.right", 0, 0, 1000));

    int lineMarginWidth = 42;
    if (!properties_.boolean_for_file("line.margin.visible", true)) {
        lineMarginWidth = 0;
    } else if (const auto configuredWidth = properties_.value_for_file("line.margin.width")) {
        int digits = std::clamp(static_cast<int>(std::strtol(configuredWidth->c_str(), nullptr, 10)), 1, 12);
        if (configuredWidth->find('+') != std::string::npos) {
            int lineDigits = 1;
            for (LRESULT lines = std::max<LRESULT>(1, send(SCI_GETLINECOUNT)); lines >= 10; lines /= 10) ++lineDigits;
            digits = std::max(digits, lineDigits);
        }
        const std::string sample(static_cast<std::size_t>(digits), '9');
        lineMarginWidth = static_cast<int>(send(SCI_TEXTWIDTH, STYLE_LINENUMBER,
            reinterpret_cast<LPARAM>(sample.c_str()))) + 8;
    }
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(SCI_SETMARGINWIDTHN, 0, lineMarginWidth);
    const int marginCursor = properties_.integer_for_file("margin.cursor", SC_CURSORREVERSEARROW,
        SC_CURSORNORMAL, SC_CURSORREVERSEARROW);
    send(SCI_SETMARGINCURSORN, 0, marginCursor);

    const int markerMarginWidth = properties_.integer_for_file("margin.width", 0, 0, 1000);
    send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINMASKN, 1, static_cast<LPARAM>(~static_cast<unsigned>(SC_MASK_FOLDERS)));
    send(SCI_SETMARGINWIDTHN, 1, markerMarginWidth);
    send(SCI_SETMARGINCURSORN, 1, marginCursor);

    const bool folding = properties_.boolean_for_file("fold", false);
    send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINMASKN, 2, static_cast<LPARAM>(SC_MASK_FOLDERS));
    send(SCI_SETMARGINSENSITIVEN, 2, folding);
    const int foldMarginWidth = properties_.integer_for_file("fold.margin.width", 14, 0, 1000);
    send(SCI_SETMARGINWIDTHN, 2, folding ? foldMarginWidth : 0);
    send(SCI_SETMARGINCURSORN, 2, marginCursor);
    const int foldSymbols = properties_.integer_for_file("fold.symbols", 1, 0, 3);
    if (foldSymbols == 0) {
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_ARROWDOWN);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_ARROW);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_ARROW);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_ARROWDOWN);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY);
    } else if (foldSymbols == 1) {
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_MINUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_PLUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_PLUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_MINUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY);
    } else if (foldSymbols == 2) {
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_CIRCLEMINUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_CIRCLEPLUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNERCURVE);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_CIRCLEPLUSCONNECTED);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_CIRCLEMINUSCONNECTED);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNERCURVE);
    } else {
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    }
    send(SCI_SETFOLDFLAGS, properties_.integer_for_file("fold.flags", 0, 0, 0xffff));
    if (const auto configured = colour_property("fold.margin.colour")) {
        send(SCI_SETFOLDMARGINCOLOUR, TRUE, configured->rgb);
    } else {
        send(SCI_SETFOLDMARGINCOLOUR, FALSE, 0);
    }
    if (const auto configured = colour_property("fold.margin.highlight.colour")) {
        send(SCI_SETFOLDMARGINHICOLOUR, TRUE, configured->rgb);
    } else {
        send(SCI_SETFOLDMARGINHICOLOUR, FALSE, 0);
    }
    const auto foldFore = colour_property("fold.fore");
    const auto foldBack = colour_property("fold.back");
    const int strokeWidth = properties_.integer_for_file("fold.stroke.width", 100, 1, 1000);
    for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker) {
        if (foldFore) send(SCI_MARKERSETFORE, marker, foldFore->rgb);
        if (foldBack) send(SCI_MARKERSETBACK, marker, foldBack->rgb);
        send(SCI_MARKERSETSTROKEWIDTH, marker, strokeWidth);
    }
    const bool highlightFold = properties_.boolean_for_file("fold.highlight", false);
    send(SCI_MARKERENABLEHIGHLIGHT, highlightFold);
    if (const auto highlight = colour_property("fold.highlight.colour")) {
        for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker) {
            send(SCI_MARKERSETBACKSELECTEDTRANSLUCENT, marker, colour_alpha(*highlight));
        }
    }
    if (const auto lineColour = colour_property("fold.line.colour")) {
        send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_FOLD_LINE, colour_alpha(*lineColour));
    }

    send(SCI_SETEDGECOLUMN, static_cast<WPARAM>(properties_.integer_for_file("edge.column", 80, 0, 1000)));
    send(SCI_SETEDGEMODE, static_cast<WPARAM>(properties_.integer_for_file("edge.mode", EDGE_NONE, 0, 3)));
    if (const auto edge = colour_property("edge.colour")) send(SCI_SETEDGECOLOUR, edge->rgb);

    send(SCI_SETCARETWIDTH, properties_.integer_for_file("caret.width", 1, 0, 20));
    send(SCI_SETCARETPERIOD, properties_.integer_for_file("caret.period", 500, 0, 10000));
    send(SCI_SETCARETSTYLE, properties_.integer_for_file("caret.style", CARETSTYLE_LINE, 0, 0xff));
    send(SCI_SETADDITIONALCARETSBLINK, properties_.boolean_for_file("caret.additional.blinks", true));
    send(SCI_SETADDITIONALCARETSVISIBLE, properties_.boolean_for_file("caret.additional.visible", true));
    const bool caretLineConfigured = properties_.value_for_file("caret.line.back").has_value();
    send(SCI_SETCARETLINEVISIBLE, caretLineConfigured || properties_.boolean_for_file("caret.line.visible", false));
    send(SCI_SETCARETLINEVISIBLEALWAYS, properties_.boolean_for_file("caret.line.always.visible", true));
    send(SCI_SETCARETLINEFRAME, properties_.integer_for_file("caret.line.frame", 0, 0, 100));
    send(SCI_SETCARETLINELAYER, properties_.integer_for_file("caret.line.layer", SC_LAYER_BASE,
        SC_LAYER_BASE, SC_LAYER_OVER_TEXT));
    send(SCI_SETCARETLINEHIGHLIGHTSUBLINE,
        properties_.boolean_for_file("caret.line.highlight.subline", false));
    send(SCI_SETCARETSTICKY, properties_.integer_for_file("caret.sticky", SC_CARETSTICKY_OFF,
        SC_CARETSTICKY_OFF, SC_CARETSTICKY_WHITESPACE));

    send(SCI_SETSELECTIONLAYER, properties_.integer_for_file("selection.layer", SC_LAYER_BASE,
        SC_LAYER_BASE, SC_LAYER_OVER_TEXT));
    if (properties_.value_for_file("selection.alpha")) {
        send(SCI_SETSELALPHA, properties_.integer_for_file("selection.alpha", SC_ALPHA_NOALPHA, 0,
            SC_ALPHA_NOALPHA));
    }
    if (properties_.value_for_file("selection.additional.alpha")) {
        send(SCI_SETADDITIONALSELALPHA, properties_.integer_for_file("selection.additional.alpha",
            SC_ALPHA_NOALPHA, 0, SC_ALPHA_NOALPHA));
    }
    if (properties_.value_for_file("caret.line.back.alpha")) {
        send(SCI_SETCARETLINEBACKALPHA, properties_.integer_for_file("caret.line.back.alpha",
            SC_ALPHA_NOALPHA, 0, SC_ALPHA_NOALPHA));
    }
    send(SCI_SETSELEOLFILLED, properties_.boolean_for_file("selection.eol.filled", false));
    // A missing property must preserve Scintilla's normal visible-selection
    // behaviour. SCI_HIDESELECTION(TRUE) hides the selection even while the
    // editor is focused; it is not merely an inactive-window preference.
    send(SCI_HIDESELECTION, !properties_.boolean_for_file("selection.always.visible", true));
    send(SCI_SETMOUSESELECTIONRECTANGULARSWITCH,
        properties_.boolean_for_file("selection.rectangular.switch.mouse", false));
    send(SCI_SETMULTIPLESELECTION, properties_.boolean_for_file("selection.multiple", true));
    send(SCI_SETADDITIONALSELECTIONTYPING, properties_.boolean_for_file("selection.additional.typing", true));
    send(SCI_SETMULTIPASTE, properties_.integer_for_file("selection.multipaste", SC_MULTIPASTE_ONCE,
        SC_MULTIPASTE_ONCE, SC_MULTIPASTE_EACH));
    send(SCI_SETVIRTUALSPACEOPTIONS, properties_.integer_for_file("virtual.space", SCVS_NONE, 0,
        SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE | SCVS_NOWRAPLINESTART));
    send(SCI_SETRECTANGULARSELECTIONMODIFIER,
        properties_.integer_for_file("rectangular.selection.modifier", SCMOD_ALT, SCMOD_NORM, SCMOD_META));

    auto caret_policy = [&](const char* slopName, const char* strictName, const char* evenName,
        const char* jumpsName) {
        int policy = 0;
        if (properties_.boolean_for_file(slopName, false)) policy |= CARET_SLOP;
        if (properties_.boolean_for_file(strictName, false)) policy |= CARET_STRICT;
        if (properties_.boolean_for_file(evenName, false)) policy |= CARET_EVEN;
        if (properties_.boolean_for_file(jumpsName, false)) policy |= CARET_JUMPS;
        return policy;
    };
    send(SCI_SETXCARETPOLICY,
        caret_policy("caret.policy.xslop", "caret.policy.xstrict", "caret.policy.xeven", "caret.policy.xjumps"),
        properties_.integer_for_file("caret.policy.width", 0, 0, 10000));
    send(SCI_SETYCARETPOLICY,
        caret_policy("caret.policy.yslop", "caret.policy.ystrict", "caret.policy.yeven", "caret.policy.yjumps"),
        properties_.integer_for_file("caret.policy.lines", 0, 0, 10000));

    if (const auto characters = properties_.value_for_file("word.characters")) {
        send(SCI_SETWORDCHARS, 0, reinterpret_cast<LPARAM>(characters->c_str()));
    } else {
        send(SCI_SETCHARSDEFAULT);
    }
    if (const auto characters = properties_.value_for_file("whitespace.characters")) {
        send(SCI_SETWHITESPACECHARS, 0, reinterpret_cast<LPARAM>(characters->c_str()));
    }
    if (const auto characters = properties_.value_for_file("punctuation.characters")) {
        send(SCI_SETPUNCTUATIONCHARS, 0, reinterpret_cast<LPARAM>(characters->c_str()));
    }

    send(SCI_AUTOCSETCHOOSESINGLE, properties_.boolean_for_file("autocomplete.choose.single", false));
    send(SCI_AUTOCSETMAXHEIGHT,
        properties_.integer_for_file("autocomplete.visible.item.count", 5, 1, 100));
    send(SCI_AUTOCSETIGNORECASE, properties_.boolean_for_file("autocomplete.ignorecase", false));
    send(SCI_AUTOCSETAUTOHIDE, properties_.boolean_for_file("autocomplete.autohide", true));
    send(SCI_AUTOCSETDROPRESTOFWORD, properties_.boolean_for_file("autocomplete.drop.rest.of.word", false));
    send(SCI_AUTOCSETMULTI, properties_.integer_for_file("autocomplete.multi", SC_MULTIAUTOC_ONCE,
        SC_MULTIAUTOC_ONCE, SC_MULTIAUTOC_EACH));
    if (const auto fillups = properties_.value_for_file("autocomplete.fillups")) {
        send(SCI_AUTOCSETFILLUPS, 0, reinterpret_cast<LPARAM>(fillups->c_str()));
    }
    auto list_colour = [&](const char* name, int element) {
        if (const auto configured = colour_property(name)) {
            send(SCI_SETELEMENTCOLOUR, element, colour_alpha(*configured));
        }
    };
    list_colour("autocomplete.fore", SC_ELEMENT_LIST);
    list_colour("autocomplete.back", SC_ELEMENT_LIST_BACK);
    list_colour("autocomplete.selected.fore", SC_ELEMENT_LIST_SELECTED);
    list_colour("autocomplete.selected.back", SC_ELEMENT_LIST_SELECTED_BACK);

    send(SCI_SETHSCROLLBAR, properties_.boolean_for_file("horizontal.scrollbar", true));
    send(SCI_SETSCROLLWIDTH, properties_.integer_for_file("horizontal.scroll.width", 2000, 1, 1000000));
    send(SCI_SETSCROLLWIDTHTRACKING,
        properties_.boolean_for_file("horizontal.scroll.width.tracking", false));
    send(SCI_SETENDATLASTLINE, properties_.boolean_for_file("end.at.last.line", true));
    send(SCI_SETZOOM, properties_.integer_for_file("magnification", 0, -10, 20));
}

} // namespace editmdview
