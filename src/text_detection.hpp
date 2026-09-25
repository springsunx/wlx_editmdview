#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace editmdview {

enum class TextContentKind {
    Binary,
    EightBit,
    Utf16Le,
    Utf16Be,
};

TextContentKind detect_text_content(std::span<const std::uint8_t> bytes) noexcept;
bool is_supported_text_file(const std::filesystem::path& path) noexcept;

} // namespace editmdview
