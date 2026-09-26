#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editmdview::i18n {

void initialize(const std::filesystem::path& modulePath);
std::wstring text(std::wstring_view source);
std::string text_utf8(std::string_view source);
std::wstring language();
std::wstring selected_language();
std::vector<std::wstring> available_languages(const std::filesystem::path& modulePath);
bool select_language(const std::filesystem::path& modulePath, std::wstring_view selection,
    std::wstring& error);

} // namespace editmdview::i18n