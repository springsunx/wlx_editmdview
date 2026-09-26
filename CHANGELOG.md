# Changelog

## 0.5.10

- Added interface localization with Simplified Chinese and English catalogs, automatic Windows
  locale detection, external `lang\*.lng` catalogs, and an immediate language switcher on the main toolbar.
- Added Save As from the More menu and `Ctrl+Shift+S`, retaining the current encoding,
  line endings, editor position, privileged-save fallback, and file-type-aware preview/highlighting.
- Fixed editor content shifting after closing and reopening Lister, including wrapped long
  lines and cases where the caret is outside the visible viewport.
- Restored the saved viewport after the final window layout and retained compatibility with
  view-state files written by version 0.5.9.
- Fixed `font.comment` so it supplies the default typeface and size for comment styles in every
  bundled lexer, while explicit `style.*.N` and `style.<lexer>.N` settings still take precedence.

## 0.5.9

- Added native x86 and x64 builds to one Total Commander installation package.
- Added Markdown and HTML preview, edit, and split modes with `Ctrl+M` cycling.
- Added bidirectional editor/preview navigation, synchronized scrolling, heading navigation,
  adjustable preview width, and a per-document draggable split ratio.
- Added Scintilla editing with SciTE-compatible editor properties, automatic property reload,
  language selection, folding, completion, word wrap, and configurable styles.
- Added Markdown shortcuts, slash commands, smart list continuation, paired punctuation,
  selection formatting, and smart URL paste.
- Added modeless find/replace with persistent editable history and `Shift+Delete` removal.
- Added atomic and elevated save, encoding/EOL preservation, external-change detection,
  abnormal-close recovery, and persistent per-document view state.
- Added offline code highlighting and copy buttons in rendered Markdown.
- Added content-first text detection so extensionless and uncommon text files can be opened
  without maintaining a fixed Total Commander extension whitelist.
