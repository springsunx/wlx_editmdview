#include "scite_properties.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>

namespace editmdview {
namespace {

std::string trim(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) input.remove_suffix(1);
    return std::string(input);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::wstring normalized_identity(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    std::wstring identity = (error ? path.lexically_normal() : normalized).wstring();
    std::transform(identity.begin(), identity.end(), identity.begin(), ::towlower);
    return identity;
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

std::string property_bytes_as_utf8(std::string bytes) {
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) {
        bytes.erase(0, 3);
        return bytes;
    }
    if (bytes.size() >= 2 &&
        ((static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) ||
         (static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff))) {
        const bool littleEndian = static_cast<unsigned char>(bytes[0]) == 0xff;
        std::wstring wide;
        wide.reserve((bytes.size() - 2) / 2);
        for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
            const auto first = static_cast<unsigned char>(bytes[index]);
            const auto second = static_cast<unsigned char>(bytes[index + 1]);
            wide.push_back(static_cast<wchar_t>(littleEndian ? first | (second << 8) : (first << 8) | second));
        }
        return wide_to_utf8(wide);
    }
    if (bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0) > 0) {
        return bytes;
    }
    const int required = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required <= 0) return bytes;
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), required);
    return wide_to_utf8(wide);
}

std::filesystem::path environment_path(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) return {};
    value.resize(written);
    return std::filesystem::path(value);
}

bool wildcard_match(std::string_view pattern, std::string_view text) {
    std::size_t patternIndex = 0;
    std::size_t textIndex = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' || pattern[patternIndex] == text[textIndex])) {
            ++patternIndex;
            ++textIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            star = patternIndex++;
            retry = textIndex;
        } else if (star != std::string_view::npos) {
            patternIndex = star + 1;
            textIndex = ++retry;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') ++patternIndex;
    return patternIndex == pattern.size();
}

bool matches_patterns(std::string_view patterns, const std::filesystem::path& documentPath) {
    const std::string filename = lower_ascii(path_utf8(documentPath.filename()));
    const std::string fullPath = lower_ascii(path_utf8(documentPath.lexically_normal()));
    std::size_t start = 0;
    while (start <= patterns.size()) {
        const std::size_t end = patterns.find(';', start);
        std::string pattern = lower_ascii(trim(patterns.substr(start,
            end == std::string_view::npos ? patterns.size() - start : end - start)));
        std::replace(pattern.begin(), pattern.end(), '\\', '/');
        if (!pattern.empty() && (wildcard_match(pattern, filename) || wildcard_match(pattern, fullPath))) {
            return true;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return false;
}

bool has_continuation(const std::string& line) {
    std::size_t slashes = 0;
    for (auto iterator = line.rbegin(); iterator != line.rend() && *iterator == '\\'; ++iterator) ++slashes;
    return (slashes % 2) != 0;
}

bool is_generic_import(const std::filesystem::path& path) {
    const std::wstring name = path.filename().wstring();
    return _wcsicmp(name.c_str(), L"SciTEGlobal.properties") == 0 ||
        _wcsicmp(name.c_str(), L"SciTEUser.properties") == 0 ||
        _wcsicmp(name.c_str(), L"SciTEDirectory.properties") == 0 ||
        _wcsicmp(name.c_str(), L"SciTE.properties") == 0 ||
        _wcsicmp(name.c_str(), L"abbrev.properties") == 0;
}

} // namespace

SciteProperties::SciteProperties(std::filesystem::path documentPath, bool dark)
    : documentPath_(std::move(documentPath)) {
    const auto& path = documentPath_;
    set_builtin("FilePath", path_utf8(path));
    set_builtin("FileDir", path_utf8(path.parent_path()));
    set_builtin("FileName", path_utf8(path.stem()));
    std::string extension = path_utf8(path.extension());
    if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    set_builtin("FileExt", extension);
    set_builtin("FileNameExt", path_utf8(path.filename()));
    set_builtin("Appearance", dark ? "1" : "0");
    set_builtin("PLAT_WIN", "1");
    set_builtin("PLAT_GTK", "0");
    set_builtin("PLAT_MAC", "0");
    set_builtin("PLAT_UNIX", "0");
}

SciteProperties SciteProperties::load_for_document(const std::filesystem::path& modulePath,
    const std::filesystem::path& documentPath, bool dark) {
    SciteProperties properties(documentPath, dark);
    const std::filesystem::path pluginHome = modulePath.parent_path();
    const std::filesystem::path configuredHome = environment_path(L"SciTE_HOME");
    const std::filesystem::path configuredUserHome = environment_path(L"SciTE_USERHOME");
    const std::filesystem::path defaultHome = configuredHome.empty() ? pluginHome : configuredHome;
    std::filesystem::path userHome = configuredUserHome;
    if (userHome.empty()) userHome = configuredHome.empty() ? environment_path(L"USERPROFILE") : configuredHome;
    if (userHome.empty()) userHome = pluginHome;

    properties.set_builtin("SciteDefaultHome", path_utf8(defaultHome));
    properties.set_builtin("SciteUserHome", path_utf8(userHome));
    // The plugin directory is always a portable configuration root. Explicit
    // SciTE homes may then override its global defaults, while the portable
    // user file is intentionally loaded after the standard user file.
    properties.load_file(pluginHome / L"SciTEGlobal.properties");
    if (!configuredHome.empty() && configuredHome != pluginHome) {
        properties.load_file(configuredHome / L"SciTEGlobal.properties");
    }
    properties.load_file(userHome / L"SciTEUser.properties");
    if (userHome != pluginHome) {
        properties.load_file(pluginHome / L"SciTEUser.properties");
    }

    if (properties.boolean_for_file("properties.directory.enable", false)) {
        std::filesystem::path directory = documentPath.parent_path();
        while (!directory.empty()) {
            const std::filesystem::path candidate = directory / L"SciTEDirectory.properties";
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                properties.load_file(candidate);
                break;
            }
            const std::filesystem::path parent = directory.parent_path();
            if (parent == directory) break;
            directory = parent;
        }
    }
    if (properties.boolean_for_file("properties.local.enable", true)) {
        properties.load_file(documentPath.parent_path() / L"SciTE.properties");
    }
    return properties;
}

void SciteProperties::load_file(const std::filesystem::path& path) {
    load_file_impl(path, path.parent_path(), 0);
}

void SciteProperties::load_file_impl(const std::filesystem::path& path,
    const std::filesystem::path& importBase, unsigned depth) {
    if (depth > 24) return;
    const std::wstring identity = normalized_identity(path);
    watchedFiles_.insert(identity);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return;
    if (!loadedFiles_.insert(identity).second) return;

    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::istringstream input(property_bytes_as_utf8(std::move(bytes)));
    if (depth == 0) {
        std::error_code relativeError;
        const auto relative = std::filesystem::relative(documentPath_, importBase, relativeError);
        set_builtin("RelativePath", path_utf8(relativeError ? documentPath_.filename() : relative));
    }
    std::string physical;
    std::string logical;
    struct ConditionalBlock {
        std::size_t indentation;
        bool active;
    };
    std::vector<ConditionalBlock> conditions;
    auto parse_line = [&](std::string line) {
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped.front() == '#') return;
        const std::size_t indentation = line.find_first_not_of(" \t");
        const std::size_t indentationWidth = indentation == std::string::npos ? line.size() : indentation;
        while (!conditions.empty() && indentationWidth <= conditions.back().indentation) conditions.pop_back();
        const bool parentActive = conditions.empty() || conditions.back().active;
        if (stripped.rfind("if ", 0) == 0 || stripped.rfind("if\t", 0) == 0) {
            conditions.push_back({indentationWidth,
                parentActive && evaluate_condition(trim(std::string_view(stripped).substr(2)))});
            return;
        }
        if (stripped.rfind("match ", 0) == 0 || stripped.rfind("match\t", 0) == 0) {
            const std::string pattern = trim(std::string_view(stripped).substr(5));
            std::filesystem::path relative(utf8_to_wide(
                builtins_.contains("RelativePath") ? builtins_.at("RelativePath") : path_utf8(documentPath_.filename())));
            conditions.push_back({indentationWidth,
                parentActive && matches_patterns(pattern, relative)});
            return;
        }
        if (!parentActive) return;
        if (stripped.rfind("import ", 0) == 0 || stripped.rfind("import\t", 0) == 0) {
            const std::string target = trim(std::string_view(stripped).substr(6));
            if (target == "*") {
                watchedDirectories_.insert(normalized_identity(importBase));
                std::vector<std::filesystem::path> imports;
                std::unordered_set<std::string> excluded;
                if (const auto exclusions = value("imports.exclude")) {
                    std::istringstream names(*exclusions);
                    std::string name;
                    while (names >> name) excluded.insert(lower_ascii(name));
                }
                std::error_code iterationError;
                for (const auto& item : std::filesystem::directory_iterator(importBase, iterationError)) {
                    if (iterationError) break;
                    if (item.is_regular_file() && _wcsicmp(item.path().extension().c_str(), L".properties") == 0 &&
                        !is_generic_import(item.path()) &&
                        !excluded.contains(lower_ascii(path_utf8(item.path().stem())))) imports.push_back(item.path());
                }
                std::sort(imports.begin(), imports.end());
                for (const auto& imported : imports) load_file_impl(imported, importBase, depth + 1);
            } else {
                std::filesystem::path imported(utf8_to_wide(expand(target)));
                if (!imported.has_extension()) imported += L".properties";
                if (imported.is_relative()) imported = importBase / imported;
                load_file_impl(imported, importBase, depth + 1);
            }
            return;
        }
        const std::size_t delimiter = stripped.find('=');
        if (delimiter == std::string::npos) return;
        std::string key = trim(std::string_view(stripped).substr(0, delimiter));
        std::string valueText = trim(std::string_view(stripped).substr(delimiter + 1));
        if (key.empty()) return;
        entries_.push_back({key, valueText});
        values_[std::move(key)] = std::move(valueText);
    };

    while (std::getline(input, physical)) {
        if (!physical.empty() && physical.back() == '\r') physical.pop_back();
        if (has_continuation(physical)) {
            physical.pop_back();
            logical += physical;
            continue;
        }
        logical += physical;
        parse_line(std::move(logical));
        logical.clear();
    }
    if (!logical.empty()) parse_line(std::move(logical));
}

std::optional<std::string> SciteProperties::raw_value(std::string_view key) const {
    const auto property = values_.find(std::string(key));
    if (property != values_.end()) return property->second;
    const auto builtin = builtins_.find(std::string(key));
    if (builtin != builtins_.end()) return builtin->second;
    const std::wstring wideKey = utf8_to_wide(key);
    const DWORD required = GetEnvironmentVariableW(wideKey.c_str(), nullptr, 0);
    if (required == 0) return std::nullopt;
    std::wstring wideValue(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(wideKey.c_str(), wideValue.data(), required);
    if (written == 0 || written >= required) return std::nullopt;
    wideValue.resize(written);
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideValue.data(), static_cast<int>(wideValue.size()),
        nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideValue.data(), static_cast<int>(wideValue.size()),
        result.data(), utf8Length, nullptr, nullptr);
    return result;
}

std::string SciteProperties::expand(std::string_view input) const {
    std::unordered_set<std::string> resolving;
    return expand_impl(input, resolving, 0);
}

std::string SciteProperties::expand_impl(std::string_view input,
    std::unordered_set<std::string>& resolving, unsigned depth) const {
    if (depth > 32) return std::string(input);
    std::string result;
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const std::size_t opening = input.find("$(", cursor);
        if (opening == std::string_view::npos) {
            result.append(input.substr(cursor));
            break;
        }
        result.append(input.substr(cursor, opening - cursor));
        const std::size_t closing = input.find(')', opening + 2);
        if (closing == std::string_view::npos) {
            result.append(input.substr(opening));
            break;
        }
        const std::string name(input.substr(opening + 2, closing - opening - 2));
        if (resolving.insert(name).second) {
            if (name.rfind("scale ", 0) == 0) {
                const std::string number = trim(std::string_view(name).substr(6));
                char* end = nullptr;
                const double logicalPixels = std::strtod(number.c_str(), &end);
                if (end != number.c_str()) {
                    const UINT dpi = GetDpiForSystem();
                    result += std::to_string(static_cast<int>(logicalPixels * dpi / 96.0 + 0.5));
                }
            } else if (const auto replacement = raw_value(name)) {
                result += expand_impl(*replacement, resolving, depth + 1);
            }
            resolving.erase(name);
        }
        cursor = closing + 1;
    }
    return result;
}

std::optional<std::string> SciteProperties::value(std::string_view key) const {
    if (const auto direct = raw_value(key)) return expand(*direct);
    for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
        if (expand(iterator->key) == key) return expand(iterator->value);
    }
    return std::nullopt;
}

bool SciteProperties::evaluate_condition(std::string_view expression) const {
    const std::string condition = trim(expression);
    auto evaluate_comparison = [&](std::string_view body, bool equal) {
        std::size_t nesting = 0;
        for (std::size_t index = 0; index < body.size(); ++index) {
            if (index + 1 < body.size() && body[index] == '$' && body[index + 1] == '(') {
                ++nesting;
                ++index;
            } else if (body[index] == ')' && nesting > 0) {
                --nesting;
            } else if (body[index] == ';' && nesting == 0) {
                const bool same = expand(trim(body.substr(0, index))) == expand(trim(body.substr(index + 1)));
                return equal ? same : !same;
            }
        }
        return false;
    };
    if (condition.rfind("$(= ", 0) == 0 && condition.back() == ')') {
        return evaluate_comparison(std::string_view(condition).substr(4, condition.size() - 5), true);
    }
    if (condition.rfind("$(!= ", 0) == 0 && condition.back() == ')') {
        return evaluate_comparison(std::string_view(condition).substr(5, condition.size() - 6), false);
    }
    std::string evaluated = condition;
    if (condition.rfind("$(", 0) == 0 && condition.back() == ')') evaluated = expand(condition);
    else if (const auto symbol = value(condition)) evaluated = *symbol;
    evaluated = lower_ascii(trim(evaluated));
    return !(evaluated.empty() || evaluated == "0" || evaluated == "false" ||
        evaluated == "no" || evaluated == "off");
}

std::optional<std::string> SciteProperties::value_for_file(std::string_view key) const {
    std::optional<std::string> result = value(key);
    const std::string prefix = std::string(key) + ".";
    for (const Entry& entry : entries_) {
        const std::string expandedKey = expand(entry.key);
        if (expandedKey.rfind(prefix, 0) != 0) continue;
        if (matches_patterns(std::string_view(expandedKey).substr(prefix.size()), documentPath_)) {
            result = expand(entry.value);
        }
    }
    return result;
}

bool SciteProperties::boolean_for_file(std::string_view key, bool fallback) const {
    const auto configured = value_for_file(key);
    if (!configured) return fallback;
    const std::string normalized = lower_ascii(trim(*configured));
    return !(normalized.empty() || normalized == "0" || normalized == "false" ||
        normalized == "no" || normalized == "off");
}

int SciteProperties::integer_for_file(std::string_view key, int fallback, int minimum, int maximum) const {
    const auto configured = value_for_file(key);
    if (!configured) return fallback;
    const std::string normalized = trim(*configured);
    int result = fallback;
    const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), result);
    if (parsed.ptr == normalized.data()) return fallback;
    return std::clamp(result, minimum, maximum);
}

std::uint64_t SciteProperties::configuration_signature() const {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    auto append = [&](std::string_view bytes) {
        for (const unsigned char byte : bytes) {
            hash ^= byte;
            hash *= prime;
        }
        hash ^= 0xff;
        hash *= prime;
    };

    std::set<std::wstring> files(watchedFiles_.begin(), watchedFiles_.end());
    for (const std::wstring& directoryName : watchedDirectories_) {
        std::error_code error;
        const std::filesystem::path directory(directoryName);
        for (std::filesystem::directory_iterator iterator(directory, error), end;
                !error && iterator != end; iterator.increment(error)) {
            if (!iterator->is_regular_file(error) ||
                _wcsicmp(iterator->path().extension().c_str(), L".properties") != 0) continue;
            files.insert(normalized_identity(iterator->path()));
        }
    }

    for (const std::wstring& fileName : files) {
        append(path_utf8(fileName));
        std::ifstream input(std::filesystem::path(fileName), std::ios::binary);
        if (!input) {
            append("<missing>");
            continue;
        }
        std::array<char, 8192> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            append(std::string_view(buffer.data(), static_cast<std::size_t>(input.gcount())));
        }
    }
    return hash;
}

void SciteProperties::set_builtin(std::string key, std::string value) {
    builtins_[std::move(key)] = std::move(value);
}

} // namespace editmdview
