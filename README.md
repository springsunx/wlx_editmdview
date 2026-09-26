# EditMdView

EditMdView is a native 32-bit and 64-bit Total Commander Lister plugin that combines a
Scintilla text editor with live Markdown and HTML preview. It is designed as a compact,
offline-first plugin: the editor, lexers, Markdown renderer, preview assets, and syntax
highlighter are compiled into the WLX binaries.

Version 0.5.10 remembers editing and preview positions both while paging and across Total
Commander restarts, restores the correct view mode without intermediate repaint jumps, and
keeps an automatic recovery snapshot for unsaved edits. SciTE property files can be reloaded
without reopening Lister, and Markdown editing includes list continuation, paired punctuation,
selection formatting, a slash-command palette, and smart URL paste.

## Features

- Scintilla 5.6.6 editor with the Lexilla Markdown lexer
- Markdown and HTML default to preview; other supported files default to editing
- Edit, split, and preview modes with a visible active state
- Split mode keeps the rendered preview aligned with caret movement and editor scrolling
- The split divider can be dragged, highlights on hover, remembers its width per visited file,
  and returns to an even split on double-click
- Cursor, selection, editor scroll, preview scroll, view mode, and split ratio are restored across sessions
- Unsaved text is periodically snapshotted and offered for recovery after an abnormal close; saving or explicitly discarding removes the snapshot
- Clicking rendered content in split mode moves the editor caret back to the corresponding source block
- Markdown headings receive stable GitHub-style anchors; documents with two or more headings get a compact,
  evenly spaced right-side section rail that reveals one title at a time on hover, highlights the current section, and supports Chinese and duplicate titles
- Markdown preview width can be adjusted symmetrically from either invisible page-edge handle; the handle appears only near the pointer and the chosen width survives live-preview refreshes
- GitHub-flavoured Markdown through MD4C
- Fenced Markdown code blocks use embedded Highlight.js syntax highlighting, show the declared
  language, and provide a GitHub-style copy button without network access
- Direct HTML preview with local relative assets, document scripts blocked, and a restrictive content-security policy
- WebView2-based rendered preview
- UTF-8, UTF-8 BOM, UTF-16 LE/BE (including conservative BOM-less detection), and system ANSI detection
- Original encoding and line endings retained on save
- Atomic save and external-change detection
- Syntax highlighting for C/C++/Java, JavaScript/TypeScript, Python, JSON, HTML/XML,
  CSS, shell, SQL, YAML, INI/properties, Apache config, CMake, Makefile, Batch,
  PowerShell, Rust, Lua, and Lisp/Scheme
- Familiar context menu commands, word wrap, zoom, and go to line
- Clickable status-bar fields for changing the active syntax language, converting CRLF/LF/CR
  line endings, and toggling word wrap without moving the caret in the viewport
- Consistent 9pt `MS Shell Dlg` font for plugin UI controls; editor fonts remain controlled by SciTE properties
- Find and replace prefill from a single-line editor selection
- Preview find highlights every match, emphasizes the current match, loops in both directions,
  and reports the current/total match count in the status bar
- Files changed by another application reload automatically when the editor is clean; local unsaved
  changes are never overwritten and receive a warning instead
- Markdown/HTML previews from 256 KB through 8 MB render on a single background worker; newer edits
  replace queued work so stale results never overwrite the latest document
- Optional SciTE configuration for DirectWrite/font rendering, lexers, keywords, full styles,
  caret and selection, indentation, wrapping, margins, folding, completion, and save cleanup
- Automatic SciTE configuration reload, plus manual reload from the editor menu or `Ctrl+Shift+R`
- No companion DLLs in the release output

## Release contents

| File | Purpose |
| --- | --- |
| `EditMdView.wlx64` | 64-bit Total Commander plugin |
| `EditMdView.wlx` | 32-bit Total Commander plugin |
| `EditMdViewSave.exe` | Elevated save helper, used only after an access-denied save |
| `pluginst.inf` | Total Commander automatic installer metadata |
| `SciTE*.properties`, `*.properties` | Tested optional editor configuration |
| `language.ini`, `lang\*.lng` | UI language selection and UTF-8 translation catalogs |
| `SHORTCUTS.txt` | Complete Chinese keyboard reference |

The plugin uses `EditMdView.wlx64` (64-bit) or `EditMdView.wlx` (32-bit). Keep the bundled `EditMdViewSave.exe` beside the WLX files: it is launched with a UAC prompt only when Windows denies an ordinary save to a protected directory. Microsoft Edge WebView2 Runtime must be installed for rendered preview; editing remains available when the runtime is missing.
The full release ZIP also includes a compact, tested SciTE configuration at the archive root.
It retains supported language definitions for C-family files, HTML/XML, Lisp/Scheme, and
configuration files while removing SciTE-only build, run, window, menu, output, print, export,
session, Lua, API, and call-tip settings. Standalone `.wlx64` and `.wlx` files remain available for users who
prefer built-in defaults.

## Interface language

Use the **语言 / Language** button on the main toolbar to switch between automatic Windows-language detection, Simplified Chinese, English, and any additional `lang\*.lng` catalogs. The change is applied immediately and remembered per Windows user. The packaged `language.ini` defaults to `auto`; `Ctrl+Shift+R` also reloads the active catalog after manual edits.

See [Interface languages](docs/I18N.md) for the lookup order and custom-language file format.
## SciTE configuration

Existing SciTE property files can be reused. Put `SciTEGlobal.properties`, `SciTEUser.properties`,
and any supported imported language files directly beside `EditMdView.wlx64` or `EditMdView.wlx`—the plugin does not search a
`config` subdirectory. See [SciTE properties compatibility](docs/SCITE_PROPERTIES.md) for the
supported editor settings and deliberate security exclusions.

Property files and wildcard-imported `.properties` files are monitored while the plugin is
open. Changes are applied automatically without replacing the document buffer. The caret,
selection, horizontal position, and visible editor area are restored after reload. Use
`Ctrl+Shift+R` when an immediate manual reload is preferred.

## Build

Requirements:

- Windows 10 or 11
- Visual Studio 2022 Build Tools with the x86/x64 C++ workload
- PowerShell 5.1 or later

Run:

```powershell
.\scripts\build.ps1
```

The first build downloads pinned upstream source packages into `.deps`, verifies their checksums,
builds both architectures, and runs the complete test suite for each one. Outputs are written to:

- `build\bin\Release\EditMdView.wlx64`
- `build-x86\bin\Release\EditMdView.wlx`

Build only one architecture with `-Architecture x64` or `-Architecture x86`. Create the single
release package containing both architectures with:

```powershell
.\scripts\package.ps1 -Configuration Release
```

## Try without Total Commander

```powershell
.\build\bin\Release\wlx_harness.exe .\build\bin\Release\EditMdView.wlx64 .\samples\demo.md
```

The same harness is used by CTest for plugin lifecycle, editing, preview, configuration,
recovery, paging, and 32/64-bit regression tests.

## Install in Total Commander

Open **Configuration → Options → Plugins → Lister plugins (WLX) → Add**, then select
`EditMdView.wlx64` for 64-bit Total Commander or `EditMdView.wlx` for 32-bit Total Commander.
When upgrading from an earlier version, first remove the old WLX entry and then add it again.
Replacing files in the existing plugin directory does not refresh Total Commander's cached
`N_detect` value in `wincmd.ini`, so the remove-and-add step is required once when upgrading from
an extension-list build.

The plugin deliberately uses no extension whitelist. Total Commander may offer every file to the
plugin; EditMdView reads a small content sample and accepts only supported text, returning control
to the next Lister plugin for binaries. This covers special names such as `CMakeLists.txt`,
extensionless names such as `Dockerfile`, uncommon script/configuration extensions, UTF-8/ANSI text, and UTF-16 text without
continually expanding a registration string. Common executable, archive, image, document, audio/video,
and database signatures are rejected explicitly. Markdown and `.html`/`.htm`/`.xhtml`/`.shtml`
open in rendered preview; other accepted text opens in the editor.

## Keyboard

- `Ctrl+S`: save
- `Ctrl+Shift+S`: save as and continue editing the new file
- `Ctrl+F`: focus the search box
- `Ctrl+H`: open the modeless find-and-replace window while editing
- `Ctrl+M`: cycle Markdown/HTML through preview, edit, and split view
- `F3` / `Shift+F3`: find next / previous
- `Ctrl+G`: go to line
- `Ctrl+B` / `Ctrl+I`: toggle Markdown bold / italic
- `Ctrl+Shift+H` / `Ctrl+Shift+X`: toggle Markdown highlight / strikethrough
- `Ctrl+K` / `Ctrl+Backtick`: insert a Markdown link / toggle inline code
- `/` at the start of a Markdown line: open the heading, list, code, quote, table, callout, and link command palette
- Markdown list, task, ordered-list, and quote markers continue on Enter; an empty marker exits the block
- `Tab` / `Shift+Tab` changes Markdown list nesting; paired punctuation wraps a selection
- Pasting a URL over selected Markdown text creates a link
- `Ctrl+Shift+R`: reload SciTE properties immediately; edited configuration files are also detected automatically
- `Ctrl++` / `Ctrl+-` / `Ctrl+0`: zoom in / out / reset
- `Alt+Z`: toggle word wrap
- Click the word-wrap indicator in the status bar to toggle wrapping without moving the caret in the viewport

The release archive also includes `SHORTCUTS.txt`, a complete Chinese shortcut reference. Find and replace use
editable drop-down lists with up to 20 deduplicated entries each. The runtime history is saved as
`EditMdView.history.ini` beside the plugin binary, and is intentionally not included in release archives.

The project was informed by the Total Commander integration lessons in `springsunx/wlx-mdview` and
CudaLister's content-first text detection; its implementation and component architecture are independent.

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/editor.*` | Scintilla configuration, commands, Markdown smart editing, and view state |
| `src/preview.*` | WebView2 host, preview interaction, find, navigation, and scroll sync |
| `src/markdown.*` | MD4C rendering and source-to-preview mapping |
| `src/app_window.*` | Lister UI, modes, status bar, search/replace, and orchestration |
| `src/document.*` | Encoding-aware file loading and saving |
| `src/scite_properties.*` | Safe SciTE property parsing, imports, expansion, and reload tracking |
| `src/session_state.*` | Per-document view persistence and recovery metadata |
| `tests/`, `tools/wlx_harness.cpp` | Core tests and real WLX integration harness |
