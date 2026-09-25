#include "text_detection.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace editmdview {
namespace {

constexpr std::uintmax_t kMaximumSupportedFileSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kProbeSize = 64ULL * 1024ULL;

bool allowed_control_byte(std::uint8_t byte) noexcept {
    switch (byte) {
    case 7:  // Bell
    case 8:  // Backspace
    case 9:  // Tab
    case 10: // LF
    case 12: // Form feed
    case 13: // CR
    case 26: // DOS EOF
    case 27: // Escape
        return true;
    default:
        return byte >= 32;
    }
}

TextContentKind detect_bomless_utf16(std::span<const std::uint8_t> bytes) noexcept {
    const std::size_t pairCount = bytes.size() / 2;
    if (pairCount < 4) return TextContentKind::Binary;

    std::size_t evenZero = 0;
    std::size_t oddZero = 0;
    std::size_t evenText = 0;
    std::size_t oddText = 0;
    for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
        const std::uint8_t even = bytes[index];
        const std::uint8_t odd = bytes[index + 1];
        evenZero += even == 0;
        oddZero += odd == 0;
        evenText += even != 0 && allowed_control_byte(even);
        oddText += odd != 0 && allowed_control_byte(odd);
    }

    // Latin-heavy UTF-16 without a BOM has a strong alternating-NUL pattern.
    // Keep the threshold conservative so arbitrary binary data is not claimed.
    const std::size_t strong = (pairCount * 3 + 4) / 5; // at least 60%
    const std::size_t weak = pairCount / 10;             // at most 10%
    if (oddZero >= strong && evenZero <= weak && evenText >= strong) return TextContentKind::Utf16Le;
    if (evenZero >= strong && oddZero <= weak && oddText >= strong) return TextContentKind::Utf16Be;
    return TextContentKind::Binary;
}

} // namespace

TextContentKind detect_text_content(std::span<const std::uint8_t> bytes) noexcept {
    const auto starts_with = [bytes](std::initializer_list<std::uint8_t> signature) {
        return bytes.size() >= signature.size() &&
            std::equal(signature.begin(), signature.end(), bytes.begin());
    };

    if (bytes.size() >= 4 &&
        ((bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0x00 && bytes[3] == 0x00) ||
         (bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFE && bytes[3] == 0xFF))) {
        return TextContentKind::Binary; // UTF-32 is not currently editable by Document.
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) return TextContentKind::Utf16Le;
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) return TextContentKind::Utf16Be;

    // Reject common binary/container formats before the control-byte heuristic.
    // Some (notably PDF and GIF) can otherwise look like an 8-bit text stream.
    if (starts_with({0x4D, 0x5A}) ||                         // PE/EXE
        starts_with({0x7F, 0x45, 0x4C, 0x46}) ||             // ELF
        starts_with({0x50, 0x4B, 0x03, 0x04}) ||             // ZIP and OOXML
        starts_with({0x50, 0x4B, 0x05, 0x06}) ||
        starts_with({0x50, 0x4B, 0x07, 0x08}) ||
        starts_with({0x89, 0x50, 0x4E, 0x47}) ||             // PNG
        starts_with({0xFF, 0xD8, 0xFF}) ||                   // JPEG
        starts_with({0x47, 0x49, 0x46, 0x38}) ||             // GIF
        starts_with({0x25, 0x50, 0x44, 0x46, 0x2D}) ||       // PDF
        starts_with({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}) || // RAR
        starts_with({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}) || // 7z
        starts_with({0xD0, 0xCF, 0x11, 0xE0}) ||             // OLE compound document
        starts_with({0x42, 0x4D}) ||                         // BMP
        starts_with({0x52, 0x49, 0x46, 0x46}) ||             // RIFF
        starts_with({0x1F, 0x8B}) ||                         // gzip
        starts_with({0x42, 0x5A, 0x68}) ||                   // bzip2
        starts_with({0x53, 0x51, 0x4C, 0x69, 0x74, 0x65, 0x20, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x20, 0x33, 0x00})) {
        return TextContentKind::Binary;
    }

    std::size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) start = 3;
    if (bytes.size() <= start + 2) return TextContentKind::EightBit;

    const bool containsNull = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(start),
        bytes.end(), static_cast<std::uint8_t>(0)) != bytes.end();
    if (containsNull) return detect_bomless_utf16(bytes.subspan(start));

    for (std::size_t index = start; index < bytes.size(); ++index) {
        if (!allowed_control_byte(bytes[index])) return TextContentKind::Binary;
    }
    return TextContentKind::EightBit;
}

bool is_supported_text_file(const std::filesystem::path& path) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumSupportedFileSize) return false;

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::size_t wanted = static_cast<std::size_t>(std::min<std::uintmax_t>(size, kProbeSize));
    std::vector<std::uint8_t> bytes(wanted);
    if (wanted != 0) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (static_cast<std::size_t>(input.gcount()) != wanted) return false;
    }
    return detect_text_content(bytes) != TextContentKind::Binary;
}

} // namespace editmdview
