# SciTE properties compatibility

EditMdView 0.5.9 reads SciTE's editor-related `.properties` settings. Configuration is optional: the plugin uses built-in defaults when no property files are present.

The release ZIP ships `SciTEGlobal.properties`, `SciTEUser.properties`, and curated
`conf.properties`, `cpp.properties`, `html.properties`, and `lisp.properties` language files.
Their supported file patterns, lexer selection, keywords, word characters, styles, braces, and
folding data are retained. SciTE-only commands, filters, language-menu entries, comment commands,
API databases, abbreviations, and locale files are omitted.

## Loading order

Properties are loaded in this order, with later values taking precedence:

1. `SciTEGlobal.properties` beside the active `EditMdView.wlx64` or `EditMdView.wlx`.
2. `SciTEGlobal.properties` from `SciTE_HOME`, when that directory is different.
3. `SciTEUser.properties` from `SciTE_USERHOME`; otherwise from `SciTE_HOME`, or `%USERPROFILE%` when neither variable is set.
4. `SciTEUser.properties` beside the active plugin file, as the portable user override.
5. The nearest parent `SciTEDirectory.properties` when `properties.directory.enable=1`.
6. `SciTE.properties` beside the document when `properties.local.enable` is not `0`.

For a portable installation, place both the plugin and its configuration together:

```text
EditMdView\
|-- EditMdView.wlx64 (64-bit) or EditMdView.wlx (32-bit)
|-- EditMdViewSave.exe (automatic UAC save helper)
|-- SciTEGlobal.properties
|-- SciTEUser.properties
|-- conf.properties
|-- cpp.properties
|-- html.properties
`-- lisp.properties
```

This plugin directory is the normal and recommended configuration root. A `config` subdirectory
is not searched. `SciTE_HOME` and `SciTE_USERHOME` remain available for testing or for users who
already share a SciTE setup, but files beside the plugin form the portable layer and the local
`SciTEUser.properties` is the final user override.

The parser supports UTF-8, UTF-16 and legacy Windows property files, `import name`, `import *`,
`imports.exclude`, line continuations, `$(variable)` expansion, `$(scale N)`, environment
variables, `if` blocks (including `PLAT_WIN` and `=` / `!=` comparisons), and basic `match`
blocks with `*` / `?` patterns. `import *` skips SciTE's generic global, user, directory, local,
and abbreviations files.

## Supported settings

- Rendering: `technology`, `font.quality`, `font.locale`, `buffered.draw`, `phases.draw`
- Lexer selection and data: `lexer.*.ext`, `keywords` through `keywords9`, `word.characters`,
  `asp.default.language`, and properties exposed by the active Lexilla lexer
- Styles: `font.base`, `style.*.N`, `style.<lexer>.N`; attributes `fore`, `back`, `font`,
  fractional `size`, `weight`, `stretch`, `case`, `bold`, `italics`, `underlined`,
  `eolfilled`, `visible`, `changeable`, `invisiblerepresentation`, and their negative forms
- Whitespace and layout: `view.whitespace`, `view.indentation.whitespace`,
  `view.indentation.guides`, `view.indentation.examine`, `highlight.indentation.guides`,
  `view.eol`, `control.char.symbol`, `whitespace.fore/back/size`, `blank.margin.left/right`,
  `extra.ascent/descent`, `edge.mode/column/colour`, `magnification`
- Margins and folding: `line.margin.visible/width`, `margin.width/cursor`,
  `fold`, `fold.symbols`, `fold.margin.width/colour/highlight.colour`, `fold.fore/back`,
  `fold.stroke.width`, `fold.flags`, `fold.line.colour`, `fold.highlight`, `fold.on.open`,
  plus lexer folding settings such as `fold.compact/comment/preprocessor`
- Caret and selection: colours including `#RRGGBBAA`, layers, frame, width, blink period,
  additional carets, caret policies, multiple selections, multipaste, virtual space,
  rectangular selection, and selection visibility
- Indentation and wrapping: `tabsize`, `indent.size`, `use.tabs`, `indent.auto`,
  `indent.automatic/opening/closing`, `tab.indents`, `backspace.unindents`, `wrap`,
  wrapping visuals/indent, Home/End behaviour, horizontal scrolling, and end-at-last-line
- Editing behaviour: matching braces, document-word completion,
  `autocomplete.choose.single/visible.item.count`, and `xml.auto.close.tags`.
  As in SciTE, `autocompleteword.automatic` is enabled only by the exact value `1`;
  `0`, `2`, and other values leave automatic document-word completion disabled
- Markdown editing: `markdown.edit.shortcuts` enables formatting shortcuts,
  `markdown.slash.commands` enables the line-start `/` command palette, and
  `markdown.smart.editing` enables list continuation, list indentation, paired characters,
  and selected-text URL paste; all default to `1`
- Saving: `strip.trailing.spaces`, `ensure.final.line.end`, and
  `ensure.consistent.line.ends`; the cleanup is reflected in the editor, remains undoable, and
  remaps the caret, selection, and wrapped viewport to the corresponding cleaned text

Supported bundled lexer names are `markdown`, `cpp`, `javascript`, `typescript`, `python`,
`json`, `hypertext`, `html`, `xml`, `css`, `bash`, `shell`, `sql`, `yaml`, `props`,
`properties`, `conf`, `cmake`, `makefile`, `batch`, `powershell`, `rust`, `lua`, and `lisp`.

## Example

```properties
font.base=font:YaHei Consolas Hybrid,normal,size:13
technology=1
font.quality=3
font.locale=zh-Hans
file.patterns.notes=*.md;*.markdown

lexer.$(file.patterns.notes)=markdown
tabsize.$(file.patterns.notes)=2
indent.size.$(file.patterns.notes)=2
use.tabs.$(file.patterns.notes)=0
wrap.$(file.patterns.notes)=1

style.*.32=$(font.base),fore:#202124,back:#FFFFFF
style.markdown.1=fore:#0969DA,bold
caret.line.back=#F6F8FA
selection.back=#ADD6FF
fold=1
```

To keep the WLX viewer safe, EditMdView parses but never executes `command.*`, Lua extensions,
director commands, or other SciTE automation. Output-pane, session, menu, print, build, API/calltip,
and substyle settings are outside the plugin's scope. `font.monospace` is available for references
from style definitions, but the separate SciTE “Use Monospaced Font” command does not exist here.
`code.page` is not sent to Scintilla: documents are decoded on load, edited internally as UTF-8,
and saved in their original detected encoding. Advanced expression operators, `match` character
sets / brace alternatives, `module` sections, and live reload are not yet supported; reopen the
viewed file after changing a property file.
