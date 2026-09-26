#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace editmdview {

struct PersistedViewState {
    std::intptr_t anchor = 0;
    std::intptr_t caret = 0;
    int firstVisibleLine = 0;
    std::intptr_t topVisiblePosition = -1;
    int horizontalOffset = 0;
    double previewScrollFraction = 0.0;
    double splitRatio = 0.5;
    int mode = 0;
};

std::filesystem::path runtime_data_directory(const std::filesystem::path& modulePath);

std::optional<PersistedViewState> load_persisted_view_state(
    const std::filesystem::path& dataDirectory, const std::filesystem::path& documentPath);
bool save_persisted_view_state(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath, const PersistedViewState& state);

std::optional<std::string> load_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath);
bool save_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath, std::string_view utf8Text);
void remove_recovery_snapshot(const std::filesystem::path& dataDirectory,
    const std::filesystem::path& documentPath) noexcept;

} // namespace editmdview
