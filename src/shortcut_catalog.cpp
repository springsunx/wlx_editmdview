#include "shortcut_catalog.hpp"

#include <array>

namespace editmdview {
namespace {

constexpr ShortcutEntry kShortcuts[] = {
    {L"Common", L"Save", L"Ctrl + S"},
    {L"Common", L"Save As", L"Ctrl + Shift + S"},
    {L"Common", L"Find", L"Ctrl + F"},
    {L"Common", L"Find Next", L"F3"},
    {L"Common", L"Find Previous", L"Shift + F3"},
    {L"Common", L"Find and Replace", L"Ctrl + H"},
    {L"Common", L"Go to Line", L"Ctrl + G"},
    {L"Common", L"Cycle preview, edit, and split", L"Ctrl + M"},
    {L"Common", L"Reload Configuration", L"Ctrl + Shift + R"},
    {L"Common", L"Word wrap", L"Alt + Z"},

    {L"Markdown", L"Bold", L"Ctrl + B"},
    {L"Markdown", L"Italic", L"Ctrl + I"},
    {L"Markdown", L"Highlight", L"Ctrl + Shift + H"},
    {L"Markdown", L"Strikethrough", L"Ctrl + Shift + X"},
    {L"Markdown", L"Insert link", L"Ctrl + K"},
    {L"Markdown", L"Inline code", L"Ctrl + `"},
    {L"Markdown", L"Continue list or quote", L"Enter"},
    {L"Markdown", L"Indent list or quote", L"Tab"},
    {L"Markdown", L"Outdent list or quote", L"Shift + Tab"},
    {L"Markdown", L"Wrap with paired punctuation", L"(  [  {  quote  `  *  _  ~"},
    {L"Markdown", L"Paste URL as link", L"Ctrl + V over selection"},
    {L"Markdown", L"Open slash commands", L"/ at line start"},
    {L"Markdown", L"Choose slash command", L"Up / Down"},
    {L"Markdown", L"Run slash command", L"Enter / Tab"},
    {L"Markdown", L"Close slash commands", L"Esc"},

    {L"Slash commands", L"Heading 1", L"/h1"},
    {L"Slash commands", L"Heading 2", L"/h2"},
    {L"Slash commands", L"Heading 3", L"/h3"},
    {L"Slash commands", L"Bulleted list", L"/l1"},
    {L"Slash commands", L"Numbered list", L"/l2"},
    {L"Slash commands", L"Task list", L"/l3"},
    {L"Slash commands", L"Code block", L"/code"},
    {L"Slash commands", L"Quote", L"/quote"},
    {L"Slash commands", L"Table", L"/table"},
    {L"Slash commands", L"Note", L"/v1 /note"},
    {L"Slash commands", L"Important", L"/v2 /important"},
    {L"Slash commands", L"Tip", L"/v3 /tip"},
    {L"Slash commands", L"Warning", L"/v4 /warning"},
    {L"Slash commands", L"Caution", L"/v5 /caution"},
    {L"Slash commands", L"Internal link", L"/link"},

    {L"Editing", L"Undo", L"Ctrl + Z / Alt + Backspace"},
    {L"Editing", L"Redo", L"Ctrl + Y"},
    {L"Editing", L"Cut", L"Ctrl + X / Shift + Delete"},
    {L"Editing", L"Copy", L"Ctrl + C / Ctrl + Insert"},
    {L"Editing", L"Paste", L"Ctrl + V / Shift + Insert"},
    {L"Editing", L"Select All", L"Ctrl + A"},
    {L"Editing", L"Delete selection or next character", L"Delete"},
    {L"Editing", L"Delete previous character", L"Backspace / Shift + Backspace"},
    {L"Editing", L"Toggle insert or overwrite", L"Insert"},
    {L"Editing", L"Insert line break", L"Enter / Shift + Enter"},
    {L"Editing", L"Indent or accept completion", L"Tab"},
    {L"Editing", L"Outdent", L"Shift + Tab"},
    {L"Editing", L"Cancel completion or call tip", L"Esc"},
    {L"Editing", L"Duplicate selection or line", L"Ctrl + D"},
    {L"Editing", L"Cut current line", L"Ctrl + L"},
    {L"Editing", L"Delete current line", L"Ctrl + Shift + L"},
    {L"Editing", L"Duplicate current line", L"Ctrl + Shift + T"},
    {L"Editing", L"Move current line up", L"Ctrl + T"},
    {L"Editing", L"Uppercase", L"Ctrl + Shift + U"},
    {L"Editing", L"Lowercase", L"Ctrl + U"},
    {L"Editing", L"Delete next word", L"Ctrl + Delete"},
    {L"Editing", L"Delete previous word", L"Ctrl + Backspace"},
    {L"Editing", L"Delete to line end", L"Ctrl + Shift + Delete"},
    {L"Editing", L"Delete to line start", L"Ctrl + Shift + Backspace"},

    {L"Navigation and selection", L"Move one character", L"Left / Right"},
    {L"Navigation and selection", L"Select one character", L"Shift + Left / Right"},
    {L"Navigation and selection", L"Move one word", L"Ctrl + Left / Right"},
    {L"Navigation and selection", L"Select one word", L"Ctrl + Shift + Left / Right"},
    {L"Navigation and selection", L"Move by word part", L"Ctrl + / or Ctrl + \\"},
    {L"Navigation and selection", L"Select by word part", L"Ctrl + Shift + / or Ctrl + Shift + \\"},
    {L"Navigation and selection", L"Move one line", L"Up / Down"},
    {L"Navigation and selection", L"Select one line", L"Shift + Up / Down"},
    {L"Navigation and selection", L"Scroll view one line", L"Ctrl + Up / Down"},
    {L"Navigation and selection", L"Move by paragraph", L"Ctrl + [ / ]"},
    {L"Navigation and selection", L"Select by paragraph", L"Ctrl + Shift + [ / ]"},
    {L"Navigation and selection", L"Move to line start or end", L"Home / End"},
    {L"Navigation and selection", L"Select to line start or end", L"Shift + Home / End"},
    {L"Navigation and selection", L"Move to visual line edge", L"Alt + Home / End"},
    {L"Navigation and selection", L"Move to document start or end", L"Ctrl + Home / End"},
    {L"Navigation and selection", L"Select to document start or end", L"Ctrl + Shift + Home / End"},
    {L"Navigation and selection", L"Page up or down", L"Page Up / Page Down"},
    {L"Navigation and selection", L"Select by page", L"Shift + Page Up / Page Down"},

    {L"Column selection", L"Extend column selection horizontally", L"Alt + Shift + Left / Right"},
    {L"Column selection", L"Extend column selection vertically", L"Alt + Shift + Up / Down"},
    {L"Column selection", L"Extend column selection to line edge", L"Alt + Shift + Home / End"},
    {L"Column selection", L"Extend column selection by page", L"Alt + Shift + Page Up / Page Down"},

    {L"Display", L"Zoom in", L"Ctrl + + / Ctrl + Num +"},
    {L"Display", L"Zoom out", L"Ctrl + - / Ctrl + Num -"},
    {L"Display", L"Reset zoom", L"Ctrl + 0 / Ctrl + Num /"},
    {L"Display", L"Zoom with mouse wheel", L"Ctrl + Mouse Wheel"},

    {L"Search fields", L"Find next from search field", L"Enter"},
    {L"Search fields", L"Find previous from search field", L"Shift + Enter"},
    {L"Search fields", L"Delete selected search history", L"Shift + Delete"},
    {L"Search fields", L"Move between find and replace controls", L"Tab / Shift + Tab"},
    {L"Search fields", L"Run current find or replace action", L"Enter"},
    {L"Search fields", L"Close or return to document", L"Esc"},

    {L"Shortcut window", L"Focus shortcut search", L"Ctrl + F"},
    {L"Shortcut window", L"Move to shortcut results", L"Down"},
    {L"Shortcut window", L"Move between shortcut controls", L"Tab / Shift + Tab"},
    {L"Shortcut window", L"Close shortcut window", L"Esc"},
};

} // namespace

std::span<const ShortcutEntry> shortcut_catalog() noexcept {
    return kShortcuts;
}

} // namespace editmdview
