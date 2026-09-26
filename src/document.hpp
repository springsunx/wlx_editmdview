#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace editmdview {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Utf16LeNoBom,
    Utf16BeNoBom,
    Ansi,
};

class Document {
public:
    using PrivilegedSaveHandler = std::function<bool(const std::filesystem::path& stagedFile,
        const std::filesystem::path& targetFile, std::wstring& error)>;

    bool load(const std::filesystem::path& path, std::wstring& error);
    bool save(std::string_view utf8Text, std::wstring& error,
        const PrivilegedSaveHandler& privilegedSave = {});
    bool save_as(const std::filesystem::path& targetPath, std::string_view utf8Text,
        std::wstring& error, const PrivilegedSaveHandler& privilegedSave = {});
    bool changed_on_disk() const;

    const std::filesystem::path& path() const noexcept { return path_; }
    const std::string& text() const noexcept { return text_; }
    TextEncoding encoding() const noexcept { return encoding_; }
    std::wstring encoding_name() const;
    const wchar_t* eol_name() const noexcept { return eolName_.c_str(); }
    bool is_markdown() const noexcept;
    bool is_html() const noexcept;
    bool supports_preview() const noexcept;

private:
    bool save_to(const std::filesystem::path& targetPath, std::string_view utf8Text,
        bool rejectExternalChanges, std::wstring& error,
        const PrivilegedSaveHandler& privilegedSave);
    bool capture_file_stamp(std::wstring& error);

    std::filesystem::path path_;
    std::string text_;
    TextEncoding encoding_ = TextEncoding::Utf8;
    std::wstring eolName_ = L"LF";
    std::filesystem::file_time_type lastWriteTime_{};
    std::uintmax_t fileSize_ = 0;
};

} // namespace editmdview
