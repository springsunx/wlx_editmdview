#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editmdview {

// A deliberately editor-only subset of SciTE's properties system. It parses the
// same files and syntax but never evaluates command.*, Lua, or other executable
// configuration.
class SciteProperties {
public:
    explicit SciteProperties(std::filesystem::path documentPath = {}, bool dark = false);

    static SciteProperties load_for_document(const std::filesystem::path& modulePath,
        const std::filesystem::path& documentPath, bool dark);

    void load_file(const std::filesystem::path& path);
    std::optional<std::string> value(std::string_view key) const;
    std::optional<std::string> value_for_file(std::string_view key) const;
    bool boolean_for_file(std::string_view key, bool fallback) const;
    int integer_for_file(std::string_view key, int fallback, int minimum, int maximum) const;
    std::uint64_t configuration_signature() const;

private:
    struct Entry {
        std::string key;
        std::string value;
    };

    void load_file_impl(const std::filesystem::path& path, const std::filesystem::path& importBase,
        unsigned depth);
    std::string expand(std::string_view input) const;
    std::string expand_impl(std::string_view input, std::unordered_set<std::string>& resolving,
        unsigned depth) const;
    std::optional<std::string> raw_value(std::string_view key) const;
    bool evaluate_condition(std::string_view expression) const;
    void set_builtin(std::string key, std::string value);

    std::filesystem::path documentPath_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::string> values_;
    std::unordered_map<std::string, std::string> builtins_;
    std::unordered_set<std::wstring> loadedFiles_;
    std::unordered_set<std::wstring> watchedFiles_;
    std::unordered_set<std::wstring> watchedDirectories_;
};

} // namespace editmdview
