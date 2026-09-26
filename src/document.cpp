#include "document.hpp"
#include "i18n.hpp"
#include "text_detection.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace editmdview {
namespace {

constexpr std::uintmax_t kMaximumFileSize = 64ULL * 1024ULL * 1024ULL;

bool valid_utf8(std::string_view value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = bytes[index++];
        if (first <= 0x7F) {
            continue;
        }

        int continuationCount = 0;
        std::uint32_t codePoint = 0;
        if ((first & 0xE0) == 0xC0) {
            continuationCount = 1;
            codePoint = first & 0x1F;
            if (codePoint == 0) return false;
        } else if ((first & 0xF0) == 0xE0) {
            continuationCount = 2;
            codePoint = first & 0x0F;
        } else if ((first & 0xF8) == 0xF0) {
            continuationCount = 3;
            codePoint = first & 0x07;
        } else {
            return false;
        }

        if (index + continuationCount > value.size()) return false;
        for (int i = 0; i < continuationCount; ++i) {
            const unsigned char next = bytes[index++];
            if ((next & 0xC0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3F);
        }

        if ((continuationCount == 1 && codePoint < 0x80) ||
            (continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) ||
            codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return false;
        }
    }
    return true;
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

std::wstring bytes_to_wide(std::string_view value, UINT codePage, DWORD flags = 0) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string decode_utf16(const std::vector<std::uint8_t>& bytes, bool bigEndian, std::size_t offset) {
    if (bytes.size() < offset) return {};
    const std::size_t unitCount = (bytes.size() - offset) / 2;
    std::wstring text(unitCount, L'\0');
    for (std::size_t i = 0; i < unitCount; ++i) {
        const auto first = bytes[offset + i * 2];
        const auto second = bytes[offset + i * 2 + 1];
        text[i] = static_cast<wchar_t>(bigEndian ? ((first << 8) | second) : (first | (second << 8)));
    }
    return wide_to_utf8(text);
}

std::vector<std::uint8_t> encode_text(std::string_view utf8, TextEncoding encoding, std::wstring& error) {
    std::vector<std::uint8_t> output;
    if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom) {
        if (encoding == TextEncoding::Utf8Bom) {
            output.insert(output.end(), {0xEF, 0xBB, 0xBF});
        }
        output.insert(output.end(), utf8.begin(), utf8.end());
        return output;
    }

    const std::wstring wide = bytes_to_wide(utf8, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!utf8.empty() && wide.empty()) {
        error = i18n::text(L"The edited content is not valid UTF-8 text.");
        return {};
    }

    if (encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be ||
        encoding == TextEncoding::Utf16LeNoBom || encoding == TextEncoding::Utf16BeNoBom) {
        const bool bigEndian = encoding == TextEncoding::Utf16Be || encoding == TextEncoding::Utf16BeNoBom;
        const bool withBom = encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be;
        if (withBom) {
            output.insert(output.end(), bigEndian ? std::initializer_list<std::uint8_t>{0xFE, 0xFF}
                                                  : std::initializer_list<std::uint8_t>{0xFF, 0xFE});
        }
        output.reserve(output.size() + wide.size() * 2);
        for (wchar_t unit : wide) {
            const auto low = static_cast<std::uint8_t>(unit & 0xFF);
            const auto high = static_cast<std::uint8_t>((unit >> 8) & 0xFF);
            output.push_back(bigEndian ? high : low);
            output.push_back(bigEndian ? low : high);
        }
        return output;
    }

    BOOL usedDefault = FALSE;
    const int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, &usedDefault);
    if (required < 0 || usedDefault) {
        error = i18n::text(L"Some characters cannot be represented by the current system encoding. Save as UTF-8 instead.");
        return {};
    }
    output.resize(static_cast<std::size_t>(required));
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()),
        reinterpret_cast<char*>(output.data()), required, nullptr, &usedDefault);
    if (usedDefault) {
        error = i18n::text(L"Some characters cannot be represented by the current system encoding. Save as UTF-8 instead.");
        return {};
    }
    return output;
}

std::wstring windows_error(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count && buffer ? std::wstring(buffer, count) : i18n::text(L"Unknown error");
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}

bool permission_error(DWORD code) noexcept {
    return code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD;
}

bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
    DWORD creationDisposition, DWORD attributes, DWORD& failure) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, creationDisposition,
        attributes, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        failure = GetLastError();
        return false;
    }

    bool ok = true;
    std::size_t writtenTotal = 0;
    while (writtenTotal < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - writtenTotal,
            std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        const BOOL writeSucceeded = WriteFile(file, bytes.data() + writtenTotal, chunk, &written, nullptr);
        if (!writeSucceeded || written == 0) {
            failure = writeSucceeded ? ERROR_WRITE_FAULT : GetLastError();
            ok = false;
            break;
        }
        writtenTotal += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        failure = GetLastError();
        ok = false;
    }
    CloseHandle(file);
    return ok;
}

std::filesystem::path adjacent_temporary_path(const std::filesystem::path& target,
    unsigned int attempt) {
    return target.wstring() + L".editmdview." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L"." + std::to_wstring(GetTickCount64()) + L"." +
        std::to_wstring(attempt) + L".tmp";
}

bool create_staging_file(const std::vector<std::uint8_t>& bytes,
    std::filesystem::path& staging, std::wstring& error) {
    std::wstring tempDirectory(MAX_PATH + 1, L'\0');
    const DWORD directoryLength = GetTempPathW(static_cast<DWORD>(tempDirectory.size()),
        tempDirectory.data());
    if (directoryLength == 0 || directoryLength >= tempDirectory.size()) {
        error = i18n::text(L"Unable to obtain a temporary folder for elevated saving:") + L" " + windows_error(GetLastError());
        return false;
    }
    tempDirectory.resize(directoryLength);

    std::wstring tempFile(MAX_PATH + 1, L'\0');
    if (!GetTempFileNameW(tempDirectory.c_str(), L"EMV", 0, tempFile.data())) {
        error = i18n::text(L"Unable to create the temporary file for elevated saving:") + L" " + windows_error(GetLastError());
        return false;
    }
    tempFile.resize(wcslen(tempFile.c_str()));
    staging = tempFile;

    DWORD failure = ERROR_SUCCESS;
    if (!write_bytes(staging, bytes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, failure)) {
        DeleteFileW(staging.c_str());
        staging.clear();
        error = i18n::text(L"Unable to write the temporary file for elevated saving:") + L" " + windows_error(failure);
        return false;
    }
    return true;
}

} // namespace

bool Document::load(const std::filesystem::path& path, std::wstring& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = i18n::text(L"Unable to read the file size.");
        return false;
    }
    if (size > kMaximumFileSize) {
        error = i18n::text(L"Files larger than 64 MB are not supported.");
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = i18n::text(L"Unable to open the file.");
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            error = i18n::text(L"An error occurred while reading the file.");
            return false;
        }
    }

    encoding_ = TextEncoding::Utf8;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        encoding_ = TextEncoding::Utf8Bom;
        text_.assign(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
    } else if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        encoding_ = TextEncoding::Utf16Le;
        text_ = decode_utf16(bytes, false, 2);
    } else if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        encoding_ = TextEncoding::Utf16Be;
        text_ = decode_utf16(bytes, true, 2);
    } else {
        const TextContentKind contentKind = detect_text_content(bytes);
        if (contentKind == TextContentKind::Binary) {
            error = i18n::text(L"The file appears to contain binary data.");
            return false;
        }
        if (contentKind == TextContentKind::Utf16Le || contentKind == TextContentKind::Utf16Be) {
            const bool bigEndian = contentKind == TextContentKind::Utf16Be;
            encoding_ = bigEndian ? TextEncoding::Utf16BeNoBom : TextEncoding::Utf16LeNoBom;
            text_ = decode_utf16(bytes, bigEndian, 0);
        } else {
        std::string raw;
        if (!bytes.empty()) raw.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (valid_utf8(raw)) {
            encoding_ = TextEncoding::Utf8;
            text_ = std::move(raw);
        } else {
            encoding_ = TextEncoding::Ansi;
            text_ = wide_to_utf8(bytes_to_wide(raw, CP_ACP));
        }
        }
    }

    std::size_t crlfCount = 0;
    std::size_t crCount = 0;
    std::size_t lfCount = 0;
    for (std::size_t index = 0; index < text_.size(); ++index) {
        if (text_[index] == '\r') {
            if (index + 1 < text_.size() && text_[index + 1] == '\n') {
                ++crlfCount;
                ++index;
            } else {
                ++crCount;
            }
        } else if (text_[index] == '\n') {
            ++lfCount;
        }
    }
    if (crlfCount >= crCount && crlfCount >= lfCount && crlfCount > 0) eolName_ = L"CRLF";
    else if (crCount >= lfCount && crCount > 0) eolName_ = L"CR";
    else eolName_ = L"LF";

    path_ = path;
    return capture_file_stamp(error);
}

bool Document::capture_file_stamp(std::wstring& error) {
    std::error_code ec;
    lastWriteTime_ = std::filesystem::last_write_time(path_, ec);
    if (ec) {
        error = i18n::text(L"Unable to read the file timestamp.");
        return false;
    }
    fileSize_ = std::filesystem::file_size(path_, ec);
    if (ec) {
        error = i18n::text(L"Unable to read the file size.");
        return false;
    }
    return true;
}

bool Document::changed_on_disk() const {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path_, ec);
    if (ec) return true;
    const auto size = std::filesystem::file_size(path_, ec);
    return ec || time != lastWriteTime_ || size != fileSize_;
}

bool Document::save(std::string_view utf8Text, std::wstring& error,
    const PrivilegedSaveHandler& privilegedSave) {
    return save_to(path_, utf8Text, true, error, privilegedSave);
}

bool Document::save_as(const std::filesystem::path& targetPath, std::string_view utf8Text,
    std::wstring& error, const PrivilegedSaveHandler& privilegedSave) {
    if (targetPath.empty()) {
        error = i18n::text(L"The Save As path cannot be empty.");
        return false;
    }
    const std::wstring current = path_.lexically_normal().wstring();
    const std::wstring target = targetPath.lexically_normal().wstring();
    const bool sameFile = _wcsicmp(current.c_str(), target.c_str()) == 0;
    return save_to(targetPath, utf8Text, sameFile, error, privilegedSave);
}

bool Document::save_to(const std::filesystem::path& targetPath, std::string_view utf8Text,
    bool rejectExternalChanges, std::wstring& error,
    const PrivilegedSaveHandler& privilegedSave) {
    error.clear();
    if (rejectExternalChanges && changed_on_disk()) {
        error = i18n::text(L"The file was modified by another program. Reopen it before saving to avoid overwriting external changes.");
        return false;
    }

    auto bytes = encode_text(utf8Text, encoding_, error);
    if (!error.empty()) return false;

    const auto finishSave = [&]() {
        path_ = targetPath;
        text_.assign(utf8Text);
        return capture_file_stamp(error);
    };
    const auto saveWithPrivileges = [&]() {
        if (!privilegedSave) return false;
        std::filesystem::path staging;
        if (!create_staging_file(bytes, staging, error)) return false;
        const bool saved = privilegedSave(staging, targetPath, error);
        DeleteFileW(staging.c_str());
        return saved && finishSave();
    };

    std::filesystem::path temporary;
    DWORD writeFailure = ERROR_FILE_EXISTS;
    bool temporaryWritten = false;
    for (unsigned int attempt = 0; attempt < 32; ++attempt) {
        temporary = adjacent_temporary_path(targetPath, attempt);
        if (write_bytes(temporary, bytes, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, writeFailure)) {
            temporaryWritten = true;
            break;
        }
        if (writeFailure != ERROR_FILE_EXISTS && writeFailure != ERROR_ALREADY_EXISTS) break;
    }
    if (!temporaryWritten) {
        if (writeFailure != ERROR_FILE_EXISTS && writeFailure != ERROR_ALREADY_EXISTS) {
            DeleteFileW(temporary.c_str());
        }
        if (permission_error(writeFailure) && privilegedSave) return saveWithPrivileges();
        error = i18n::text(L"Unable to create or write the temporary file:") + L" " + windows_error(writeFailure);
        return false;
    }

    if (!ReplaceFileW(targetPath.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
            nullptr, nullptr)) {
        const DWORD replaceError = GetLastError();
        if (!MoveFileExW(temporary.c_str(), targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD moveError = GetLastError();
            DeleteFileW(temporary.c_str());
            if ((permission_error(replaceError) || permission_error(moveError)) && privilegedSave) {
                return saveWithPrivileges();
            }
            error = i18n::text(L"Unable to replace the target file:") + L" " + windows_error(moveError);
            return false;
        }
    }

    return finishSave();
}
std::wstring Document::encoding_name() const {
    switch (encoding_) {
    case TextEncoding::Utf8: return L"UTF-8";
    case TextEncoding::Utf8Bom: return L"UTF-8 BOM";
    case TextEncoding::Utf16Le: return L"UTF-16 LE";
    case TextEncoding::Utf16Be: return L"UTF-16 BE";
    case TextEncoding::Utf16LeNoBom: return i18n::text(L"UTF-16 LE (no BOM)");
    case TextEncoding::Utf16BeNoBom: return i18n::text(L"UTF-16 BE (no BOM)");
    case TextEncoding::Ansi: return L"ANSI";
    }
    return L"UTF-8";
}

bool Document::is_markdown() const noexcept {
    std::wstring extension = path_.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".md" || extension == L".markdown" || extension == L".mkd" || extension == L".mkdn";
}

bool Document::is_html() const noexcept {
    std::wstring extension = path_.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".html" || extension == L".htm" || extension == L".xhtml" ||
        extension == L".shtml";
}

bool Document::supports_preview() const noexcept {
    return is_markdown() || is_html();
}

} // namespace editmdview
