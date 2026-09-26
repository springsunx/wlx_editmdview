#include "session_state.hpp"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

namespace editmdview {
namespace {

constexpr std::string_view kViewHeaderV1 = "EditMdView view state v1";
constexpr std::string_view kViewHeaderV2 = "EditMdView view state v2";
constexpr std::string_view kRecoveryHeader = "EditMdView recovery v1";
constexpr std::uintmax_t kMaximumRecoverySize = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumViewStates = 128;

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) return {};
    value.resize(written);
    return value;
}

std::wstring normalized_key(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    std::wstring key = (error ? path : absolute).lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), required, nullptr, nullptr) != required) return {};
    return result;
}

std::string hex_encode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        result += digits[byte >> 4];
        result += digits[byte & 0x0f];
    }
    return result;
}

std::optional<std::string> hex_decode(std::string_view value) {
    if ((value.size() & 1U) != 0) return std::nullopt;
    auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = nibble(value[index]);
        const int low = nibble(value[index + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result += static_cast<char>((high << 4) | low);
    }
    return result;
}

std::uint64_t path_hash(const std::filesystem::path& path) {
    const std::string key = wide_to_utf8(normalized_key(path));
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : key) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::filesystem::path keyed_path(const std::filesystem::path& dataDirectory,
    std::wstring_view folder, const std::filesystem::path& documentPath, std::wstring_view extension) {
    std::wostringstream name;
    name << std::hex << std::setw(16) << std::setfill(L'0') << path_hash(documentPath) << extension;
    return dataDirectory / folder / name.str();
}

bool ensure_parent(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error;
}

bool replace_atomically(const std::filesystem::path& temporary, const std::filesystem::path& target) {
    if (MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

std::filesystem::path temporary_path_for(const std::filesystem::path& target) {
    std::filesystem::path temporary = target;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L".tmp";
    return temporary;
}

void trim_view_states(const std::filesystem::path& directory) {
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
            iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != L".state") continue;
        files.emplace_back(iterator->last_write_time(error), iterator->path());
        if (error) break;
    }
    if (error || files.size() <= kMaximumViewStates) return;
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (std::size_t index = 0; index < files.size() - kMaximumViewStates; ++index) {
        std::filesystem::remove(files[index].second, error);
        error.clear();
    }
}

} // namespace

std::filesystem::path runtime_data_directory(const std::filesystem::path& modulePath) {
    const std::wstring overrideDirectory = environment_value(L"EDITMDVIEW_DATA_DIR");
    if (!overrideDirectory.empty()) return overrideDirectory;
    const std::wstring localAppData = environment_value(L"LOCALAPPDATA");
    if (!localAppData.empty()) return std::filesystem::path(localAppData) / L"EditMdView";
    return modulePath.parent_path() / L"data";
}

std::optional<PersistedViewState> load_persisted_view_state(
    const std::filesystem::path& dataDirectory, const std::filesystem::path& documentPath) {
    const auto path = keyed_path(dataDirectory, L"view-state", documentPath, L".state");
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string header;
    std::string encodedPath;
    PersistedViewState state;
    if (!std::getline(input, header) ||
        (header != kViewHeaderV1 && header != kViewHeaderV2) ||
        !std::getline(input, encodedPath)) return std::nullopt;
    const auto decodedPath = hex_decode(encodedPath);
    if (!decodedPath || *decodedPath != wide_to_utf8(normalized_key(documentPath))) return std::nullopt;
    long long anchor = 0;
    long long caret = 0;
    long long topVisiblePosition = -1;
    if (!(input >> anchor >> caret >> state.firstVisibleLine >> state.horizontalOffset >>
            state.previewScrollFraction >> state.splitRatio >> state.mode)) return std::nullopt;
    if (header == kViewHeaderV2 && !(input >> topVisiblePosition)) return std::nullopt;
    if (!std::isfinite(state.previewScrollFraction) || !std::isfinite(state.splitRatio) ||
        state.firstVisibleLine < 0 || topVisiblePosition < -1 || state.horizontalOffset < 0 ||
        state.mode < 0 || state.mode > 2) {
        return std::nullopt;
    }
    state.anchor = static_cast<std::intptr_t>(anchor);
    state.caret = static_cast<std::intptr_t>(caret);
    state.topVisiblePosition = static_cast<std::intptr_t>(topVisiblePosition);
    state.previewScrollFraction = std::clamp(state.previewScrollFraction, 0.0, 1.0);
    state.splitRatio = std::clamp(state.splitRatio, 0.1, 0.9);
    return state;
}

bool save_persisted_view_state(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath, const PersistedViewState& state) {
    const auto target = keyed_path(dataDirectory, L"view-state", documentPath, L".state");
    if (!ensure_parent(target)) return false;
    const auto temporary = temporary_path_for(target);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << kViewHeaderV2 << '\n' << hex_encode(wide_to_utf8(normalized_key(documentPath))) << '\n'
            << static_cast<long long>(state.anchor) << ' ' << static_cast<long long>(state.caret) << ' '
            << state.firstVisibleLine << ' ' << state.horizontalOffset << ' '
            << std::setprecision(17) << state.previewScrollFraction << ' ' << state.splitRatio << ' '
            << state.mode << ' ' << static_cast<long long>(state.topVisiblePosition) << '\n';
        output.close();
        if (!output) {
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (!replace_atomically(temporary, target)) return false;
    trim_view_states(target.parent_path());
    return true;
}

std::optional<std::string> load_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath) {
    const auto path = keyed_path(dataDirectory, L"recovery", documentPath, L".recovery");
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumRecoverySize + 65536) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string header;
    std::string encodedPath;
    std::string separator;
    if (!std::getline(input, header) || header != kRecoveryHeader ||
        !std::getline(input, encodedPath) || !std::getline(input, separator) || !separator.empty()) {
        return std::nullopt;
    }
    const auto decodedPath = hex_decode(encodedPath);
    if (!decodedPath || *decodedPath != wide_to_utf8(normalized_key(documentPath))) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool save_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath, std::string_view utf8Text) {
    if (utf8Text.size() > kMaximumRecoverySize) return false;
    const auto target = keyed_path(dataDirectory, L"recovery", documentPath, L".recovery");
    if (!ensure_parent(target)) return false;
    const auto temporary = temporary_path_for(target);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << kRecoveryHeader << '\n' << hex_encode(wide_to_utf8(normalized_key(documentPath))) << "\n\n";
        output.write(utf8Text.data(), static_cast<std::streamsize>(utf8Text.size()));
        output.close();
        if (!output) {
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    return replace_atomically(temporary, target);
}

void remove_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath) noexcept {
    const auto target = keyed_path(dataDirectory, L"recovery", documentPath, L".recovery");
    std::error_code error;
    std::filesystem::remove(target, error);
}

} // namespace editmdview
