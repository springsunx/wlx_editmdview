#pragma once

#include <span>

namespace editmdview {

struct ShortcutEntry {
    const wchar_t* group;
    const wchar_t* action;
    const wchar_t* keys;
};

std::span<const ShortcutEntry> shortcut_catalog() noexcept;

} // namespace editmdview
