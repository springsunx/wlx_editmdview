#include "i18n.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace editmdview::i18n {
namespace {

std::mutex g_mutex;
std::unordered_map<std::wstring, std::wstring> g_translations;
std::wstring g_language = L"en-US";
std::wstring g_selection = L"auto";
std::filesystem::path g_modulePath;

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::string trim_ascii(std::string value) {
    const auto whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

std::string unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 >= value.size()) {
            result += value[index];
            continue;
        }
        const char next = value[++index];
        if (next == 'n') result += '\n';
        else if (next == 'r') result += '\r';
        else if (next == 't') result += '\t';
        else result += next;
    }
    return result;
}

std::size_t separator_position(std::string_view line) {
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (!escaped && line[index] == '=') return index;
        if (!escaped && line[index] == '\\') escaped = true;
        else escaped = false;
    }
    return std::string_view::npos;
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) return {};
    value.resize(written);
    return value;
}

std::wstring lower_wide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool safe_language_name(std::wstring_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9') || character == L'-' || character == L'_';
    });
}

std::filesystem::path user_language_path() {
    const std::wstring localAppData = environment_value(L"LOCALAPPDATA");
    if (localAppData.empty()) return {};
    return std::filesystem::path(localAppData) / L"EditMdView" / L"language.ini";
}

std::wstring read_language_setting(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trim_ascii(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') continue;
        const auto separator = trimmed.find('=');
        if (separator == std::string::npos) continue;
        if (lower_wide(utf8_to_wide(trim_ascii(trimmed.substr(0, separator)))) == L"language") {
            return utf8_to_wide(trim_ascii(trimmed.substr(separator + 1)));
        }
    }
    return {};
}

std::wstring resolve_language(std::wstring selection) {
    if (selection.empty() || lower_wide(selection) == L"auto") {
        wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(localeName, static_cast<int>(std::size(localeName))) > 0) {
            selection = localeName;
        } else {
            selection = L"en-US";
        }
    }
    if (!safe_language_name(selection)) return L"en-US";
    const std::wstring lower = lower_wide(selection);
    if (lower == L"zh" || lower.starts_with(L"zh-")) return L"zh-CN";
    if (lower == L"en" || lower.starts_with(L"en-")) return L"en-US";
    return selection;
}

std::wstring configured_selection(const std::filesystem::path& modulePath) {
    std::wstring configured = environment_value(L"EDITMDVIEW_LANGUAGE");
    if (!configured.empty()) return configured;
    const auto userPath = user_language_path();
    if (!userPath.empty()) configured = read_language_setting(userPath);
    if (configured.empty()) configured = read_language_setting(modulePath.parent_path() / L"language.ini");
    return configured.empty() ? L"auto" : configured;
}
bool load_file(const std::filesystem::path& path,
    std::unordered_map<std::wstring, std::wstring>& translations) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string line;
    bool first = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        first = false;
        const std::string trimmed = trim_ascii(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') continue;
        const std::size_t separator = separator_position(trimmed);
        if (separator == std::string::npos) continue;
        const std::wstring key = utf8_to_wide(unescape(trim_ascii(trimmed.substr(0, separator))));
        const std::wstring value = utf8_to_wide(unescape(trim_ascii(trimmed.substr(separator + 1))));
        if (!key.empty()) translations.insert_or_assign(key, value);
    }
    return true;
}

} // namespace

void initialize(const std::filesystem::path& modulePath) {
    std::scoped_lock lock(g_mutex);
    const std::wstring selection = configured_selection(modulePath);
    const std::wstring requested = resolve_language(selection);
    g_modulePath = modulePath;
    g_selection = selection;
    g_language = requested;
    g_translations.clear();
    const auto languageDirectory = modulePath.parent_path() / L"lang";
    if (!load_file(languageDirectory / (g_language + L".lng"), g_translations) &&
        !lower_wide(g_language).starts_with(L"zh")) {
        g_language = L"en-US";
        load_file(languageDirectory / L"en-US.lng", g_translations);
    }
}

std::wstring text(std::wstring_view source) {
    std::scoped_lock lock(g_mutex);
    const auto found = g_translations.find(std::wstring(source));
    return found == g_translations.end() ? std::wstring(source) : found->second;
}

std::string text_utf8(std::string_view source) {
    return wide_to_utf8(text(utf8_to_wide(source)));
}

std::wstring language() {
    std::scoped_lock lock(g_mutex);
    return g_language;
}

std::wstring selected_language() {
    std::scoped_lock lock(g_mutex);
    return g_selection;
}

std::vector<std::wstring> available_languages(const std::filesystem::path& modulePath) {
    std::vector<std::wstring> languages = {L"auto", L"zh-CN", L"en-US"};
    std::error_code error;
    const auto directory = modulePath.parent_path() / L"lang";
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) continue;
        if (lower_wide(iterator->path().extension().wstring()) != L".lng") continue;
        const std::wstring code = iterator->path().stem().wstring();
        if (safe_language_name(code) && std::find(languages.begin(), languages.end(), code) == languages.end()) {
            languages.push_back(code);
        }
    }
    if (languages.size() > 3) std::sort(languages.begin() + 3, languages.end());
    return languages;
}

bool select_language(const std::filesystem::path& modulePath, std::wstring_view selection,
    std::wstring& error) {
    if (selection != L"auto" && !safe_language_name(selection)) {
        error = text(L"Invalid language identifier.");
        return false;
    }
    const auto path = user_language_path();
    if (path.empty()) {
        error = text(L"LOCALAPPDATA is unavailable.");
        return false;
    }
    std::error_code fileError;
    std::filesystem::create_directories(path.parent_path(), fileError);
    if (fileError) {
        error = text(L"Unable to create the language settings folder.");
        return false;
    }
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = text(L"Unable to write the language setting.");
            return false;
        }
        output << "language=" << wide_to_utf8(selection) << '\n';
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        error = text(L"Unable to replace the language setting.");
        return false;
    }
    {
        std::scoped_lock lock(g_mutex);
        g_modulePath.clear();
    }
    initialize(modulePath);
    error.clear();
    return true;
}

} // namespace editmdview::i18n