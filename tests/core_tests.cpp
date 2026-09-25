#include "document.hpp"
#include "html_preview.hpp"
#include "markdown.hpp"
#include "scite_properties.hpp"
#include "session_state.hpp"
#include "text_detection.hpp"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::optional<std::wstring> environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return std::nullopt;
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) return std::nullopt;
    value.resize(written);
    return value;
}

void restore_environment(const wchar_t* name, const std::optional<std::wstring>& value) {
    SetEnvironmentVariableW(name, value ? value->c_str() : nullptr);
}

} // namespace

int main() {
    using namespace editmdview;

    MarkdownRenderOptions options;
    options.title = "Demo";
    options.baseHref = "https://editmdview.local/";
    const std::wstring html = render_markdown_html("# Heading\n\n- [x] done\n\n`code`", options);
    check(html.find(L"<h1 id=\"heading\">Heading</h1>") != std::wstring::npos,
        "heading renders with a stable anchor");
    check(html.find(L"type=\"checkbox\"") != std::wstring::npos, "task list renders");
    check(html.find(L"Content-Security-Policy") != std::wstring::npos, "preview has CSP");
    check(html.find(L"name=\"editmdview-source-lines\"") != std::wstring::npos,
        "Markdown preview carries source-line navigation data");
    check(html.find(L"content=\"1,3,3,5\"") != std::wstring::npos,
        "Markdown block source lines are mapped in render order");
    const std::wstring fencedCodeHtml = render_markdown_html(
        "```cpp\nint main() { return 0; }\n```", options);
    check(fencedCodeHtml.find(L"<code class=\"language-cpp\">") != std::wstring::npos,
        "fenced code keeps its explicit language for preview highlighting");

    const std::wstring extensionHtml = render_markdown_html(
        "==highlighted== and `==literal==`\n\n> [!note]\n> useful\n\n> [!WARNING]\n> careful", options);
    check(extensionHtml.find(L"<mark>highlighted</mark>") != std::wstring::npos &&
        extensionHtml.find(L"<code>==literal==</code>") != std::wstring::npos,
        "Markdown highlight spans render without changing inline code");
    check(extensionHtml.find(L"editmdview-callout-note") != std::wstring::npos &&
        extensionHtml.find(L"editmdview-callout-warning") != std::wstring::npos &&
        extensionHtml.find(L"注释") != std::wstring::npos && extensionHtml.find(L"注意") != std::wstring::npos,
        "GitHub-style callouts render with localized styled titles");

    const std::wstring outlineHtml = render_markdown_html(
        "# 快速开始\n\n## 安装 & 配置\n\n## 安装 & 配置\n\n[返回顶部](#快速开始)", options);
    check(outlineHtml.find(L"<nav class=\"editmdview-outline\"") != std::wstring::npos &&
        outlineHtml.find(L"<span class=\"outline-mark\"") != std::wstring::npos,
        "Markdown preview provides a compact section-mark outline");
    check(outlineHtml.find(L"a:hover .outline-label") != std::wstring::npos &&
        outlineHtml.find(L".editmdview-outline:hover .outline-label") == std::wstring::npos,
        "Markdown outline reveals only the individually hovered heading");
    check(outlineHtml.find(L"justify-content:space-evenly") != std::wstring::npos &&
        outlineHtml.find(L"--outline-y") == std::wstring::npos,
        "Markdown outline spaces section marks evenly");
    check(outlineHtml.find(L".editmdview-outline{position:fixed;z-index:20;right:6px") !=
            std::wstring::npos &&
        outlineHtml.find(L"right:38px;top:50%") != std::wstring::npos,
        "Markdown outline uses a right-side rail with inward title cards");
    check(outlineHtml.find(L"class=\"editmdview-width-handle\" data-side=\"left\"") !=
            std::wstring::npos &&
        outlineHtml.find(L"class=\"editmdview-width-handle\" data-side=\"right\"") !=
            std::wstring::npos &&
        outlineHtml.find(L"cursor:col-resize") != std::wstring::npos,
        "Markdown preview provides hidden symmetric width handles");
    check(outlineHtml.find(L"<main class=\"editmdview-content\">") != std::wstring::npos,
        "Markdown content is wrapped for centered width adjustment");
    check(outlineHtml.find(L"<h1 id=\"快速开始\">") != std::wstring::npos,
        "Chinese heading anchors remain readable");
    check(outlineHtml.find(L"<h2 id=\"安装-配置\">") != std::wstring::npos &&
        outlineHtml.find(L"<h2 id=\"安装-配置-1\">") != std::wstring::npos,
        "duplicate heading anchors are unique");
    check(outlineHtml.find(L"href=\"#快速开始\"") != std::wstring::npos,
        "outline links target generated heading anchors");

    const std::wstring htmlPreview = render_html_preview(
        "<!doctype html><html><head><title>Page</title></head><body>\n<h1>Hello</h1></body></html>",
        "https://editmdview.local/");
    check(htmlPreview.find(L"<h1 data-editmdview-source-line=\"2\">Hello</h1>") != std::wstring::npos,
        "HTML preview elements carry source-line navigation data");
    check(htmlPreview.find(L"script-src 'none'") != std::wstring::npos, "HTML preview disables scripts");
    check(htmlPreview.find(L"<base href=\"https://editmdview.local/\">") != std::wstring::npos,
        "HTML preview resolves local relative resources");
    const std::wstring hostilePreview = render_html_preview(
        "<script>document.title='unsafe'</script><body onload=\"document.title='unsafe'\">safe</body>",
        "https://editmdview.local/");
    check(hostilePreview.find(L"Content-Security-Policy") < hostilePreview.find(L"<script"),
        "HTML preview installs CSP before untrusted document content");

    const auto temp = std::filesystem::temp_directory_path() /
        (L"editmdview-test-" + std::to_wstring(GetCurrentProcessId()) + L".md");
    {
        std::ofstream output(temp, std::ios::binary);
        output << "\xEF\xBB\xBF# Test\r\n";
    }

    Document document;
    std::wstring error;
    check(document.load(temp, error), "document loads");
    check(document.is_markdown() && document.supports_preview(), "Markdown supports rendered preview");
    check(document.encoding() == TextEncoding::Utf8Bom, "UTF-8 BOM detected");
    check(std::wstring(document.eol_name()) == L"CRLF", "CRLF detected");
    check(document.save("# Changed\r\n", error), "document saves");

    Document reloaded;
    check(reloaded.load(temp, error), "saved document reloads");
    check(reloaded.text() == "# Changed\r\n", "saved text preserved");
    check(reloaded.encoding() == TextEncoding::Utf8Bom, "saved encoding preserved");

    const auto mixedEol = std::filesystem::temp_directory_path() /
        (L"editmdview-eol-test-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    {
        std::ofstream output(mixedEol, std::ios::binary);
        output << "one\ntwo\r\nthree\r\nfour\r\n";
    }
    Document mixedDocument;
    check(mixedDocument.load(mixedEol, error), "mixed-EOL document loads");
    check(std::wstring(mixedDocument.eol_name()) == L"CRLF", "dominant EOL is detected");

    const auto htmlFile = std::filesystem::temp_directory_path() /
        (L"editmdview-html-test-" + std::to_wstring(GetCurrentProcessId()) + L".html");
    {
        std::ofstream output(htmlFile, std::ios::binary);
        output << "<!doctype html><h1>HTML</h1>";
    }
    Document htmlDocument;
    check(htmlDocument.load(htmlFile, error), "HTML document loads");
    check(htmlDocument.is_html() && htmlDocument.supports_preview(), "HTML supports rendered preview");

    const auto uncommonText = std::filesystem::temp_directory_path() /
        (L"editmdview-text-test-" + std::to_wstring(GetCurrentProcessId()) + L".unknown-type");
    {
        std::ofstream output(uncommonText, std::ios::binary);
        output << "#!/usr/bin/env tool\nkey=value\n中文文本\n";
    }
    check(is_supported_text_file(uncommonText),
        "unknown extensions are accepted when their content is text");

    const auto binaryFile = std::filesystem::temp_directory_path() /
        (L"editmdview-binary-test-" + std::to_wstring(GetCurrentProcessId()) + L".unknown-type");
    {
        std::ofstream output(binaryFile, std::ios::binary);
        const unsigned char bytes[] = {0x50, 0x4B, 0x03, 0x04, 0x00, 0x01, 0x02, 0x03};
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    check(!is_supported_text_file(binaryFile),
        "binary content is rejected even when the extension is unknown");

    const auto bomlessUtf16 = std::filesystem::temp_directory_path() /
        (L"editmdview-utf16-test-" + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream output(bomlessUtf16, std::ios::binary);
        const unsigned char bytes[] = {'n', 0, 'a', 0, 'm', 0, 'e', 0, '=', 0, 'v', 0, 'a', 0, 'l', 0, 'u', 0, 'e', 0};
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    check(is_supported_text_file(bomlessUtf16), "BOM-less UTF-16 text is recognized");
    Document bomlessDocument;
    check(bomlessDocument.load(bomlessUtf16, error), "BOM-less UTF-16 text loads");
    check(bomlessDocument.encoding() == TextEncoding::Utf16LeNoBom,
        "BOM-less UTF-16 encoding is retained");
    check(bomlessDocument.text() == "name=value", "BOM-less UTF-16 text decodes");
    check(bomlessDocument.save("changed", error), "BOM-less UTF-16 text saves");
    {
        std::ifstream input(bomlessUtf16, std::ios::binary);
        unsigned char prefix[2]{};
        input.read(reinterpret_cast<char*>(prefix), sizeof(prefix));
        check(prefix[0] == 'c' && prefix[1] == 0, "BOM-less UTF-16 save does not add a BOM");
    }

    const auto propertiesDirectory = std::filesystem::temp_directory_path() /
        (L"editmdview-properties-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(propertiesDirectory);
    const auto importedProperties = propertiesDirectory / L"language.properties";
    const auto mainProperties = propertiesDirectory / L"SciTEGlobal.properties";
    {
        std::ofstream output(importedProperties, std::ios::binary);
        output << "accent=#123456\n"
                  "style.*.32=font:Cascadia Mono,size:$(base.size),fore:$(accent)\n";
    }
    {
        std::ofstream output(mainProperties, std::ios::binary);
        output << "base.size=13\n"
                  "scaled.value=$(scale 16)\n"
                  "import language\n"
                  "file.patterns.test=*.foo;special.?ar\n"
                  "lexer.$(file.patterns.test)=python\n"
                  "tabsize.$(file.patterns.test)=2\n"
                  "continued=first\\\n"
                  "second\n"
                  "if PLAT_GTK\n"
                  "  platform.setting=gtk\n"
                  "if PLAT_WIN\n"
                  "  platform.setting=windows\n"
                  "if $(= $(Appearance);0)\n"
                  "  appearance.setting=light\n"
                  "match *.foo\n"
                  "  match.setting=selected\n"
                  "match *.bar\n"
                  "  match.setting=wrong\n";
    }
    SciteProperties properties(propertiesDirectory / L"sample.foo");
    properties.load_file(mainProperties);
    check(properties.value_for_file("lexer") == std::optional<std::string>("python"),
        "SciTE file pattern selects lexer");
    check(properties.integer_for_file("tabsize", 4, 1, 16) == 2,
        "SciTE file-specific integer loads");
    check(properties.value("style.*.32") ==
        std::optional<std::string>("font:Cascadia Mono,size:13,fore:#123456"),
        "SciTE imports and recursive variables expand");
    check(properties.value("continued") == std::optional<std::string>("firstsecond"),
        "SciTE continuation lines join");
    check(properties.value("platform.setting") == std::optional<std::string>("windows"),
        "SciTE platform condition selects Windows settings");
    check(properties.value("appearance.setting") == std::optional<std::string>("light"),
        "SciTE equality condition evaluates variables");
    check(properties.value("match.setting") == std::optional<std::string>("selected"),
        "SciTE match block selects the current document");
    check(properties.integer_for_file("scaled.value", 0, 0, 1000) >= 16,
        "SciTE scale function expands with system DPI");

    const auto utf16Properties = propertiesDirectory / L"utf16.properties";
    {
        std::ofstream output(utf16Properties, std::ios::binary);
        const unsigned char bom[] = {0xff, 0xfe};
        output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        const char setting[] = "font.locale=zh-Hans\r\n";
        for (std::size_t index = 0; index + 1 < sizeof(setting); ++index) {
            const char character = setting[index];
            const unsigned char encoded[] = {static_cast<unsigned char>(character), 0};
            output.write(reinterpret_cast<const char*>(encoded), sizeof(encoded));
        }
    }
    SciteProperties utf16(propertiesDirectory / L"sample.cpp");
    utf16.load_file(utf16Properties);
    check(utf16.value("font.locale") == std::optional<std::string>("zh-Hans"),
        "UTF-16 SciTE properties load");

    const std::uint64_t signatureBeforeEdit = properties.configuration_signature();
    {
        std::ofstream output(mainProperties, std::ios::binary | std::ios::app);
        output << "tabsize.*.foo=6\n";
    }
    check(properties.configuration_signature() != signatureBeforeEdit,
        "SciTE configuration signature detects an edited properties file");
    properties.load_file(mainProperties);
    check(properties.integer_for_file("tabsize", 4, 1, 16) == 2,
        "a properties layer is loaded once");

    const auto pluginHome = propertiesDirectory / L"plugin";
    const auto environmentHome = propertiesDirectory / L"environment";
    const auto userHome = propertiesDirectory / L"user";
    const auto documentHome = propertiesDirectory / L"documents";
    std::filesystem::create_directories(pluginHome);
    std::filesystem::create_directories(environmentHome);
    std::filesystem::create_directories(userHome);
    std::filesystem::create_directories(documentHome);
    {
        std::ofstream output(pluginHome / L"SciTEGlobal.properties", std::ios::binary);
        output << "portable.global=loaded\nportable.order=plugin-global\n";
    }
    {
        std::ofstream output(environmentHome / L"SciTEGlobal.properties", std::ios::binary);
        output << "portable.order=environment-global\n";
    }
    {
        std::ofstream output(userHome / L"SciTEUser.properties", std::ios::binary);
        output << "portable.order=standard-user\n";
    }
    {
        std::ofstream output(pluginHome / L"SciTEUser.properties", std::ios::binary);
        output << "portable.order=plugin-user\n";
    }
    const auto previousHome = environment_value(L"SciTE_HOME");
    const auto previousUserHome = environment_value(L"SciTE_USERHOME");
    SetEnvironmentVariableW(L"SciTE_HOME", environmentHome.c_str());
    SetEnvironmentVariableW(L"SciTE_USERHOME", userHome.c_str());
    const SciteProperties portable = SciteProperties::load_for_document(
        pluginHome / L"EditMdView.wlx64", documentHome / L"sample.foo", false);
    restore_environment(L"SciTE_HOME", previousHome);
    restore_environment(L"SciTE_USERHOME", previousUserHome);
    check(portable.value("portable.global") == std::optional<std::string>("loaded"),
        "plugin directory global properties always load");
    check(portable.value("portable.order") == std::optional<std::string>("plugin-user"),
        "plugin directory user properties are the portable override");

    const std::filesystem::path bundledConfig(EDITMDVIEW_CONFIG_DIR);
    const auto bundled_module = bundledConfig / L"EditMdView.wlx64";
    const SciteProperties lispConfig = SciteProperties::load_for_document(
        bundled_module, bundledConfig / L"sample.lisp", false);
    check(lispConfig.value_for_file("lexer") == std::optional<std::string>("lisp"),
        "bundled Lisp lexer mapping loads");
    check(lispConfig.value_for_file("keywords").value_or("").find("defun") != std::string::npos,
        "bundled Lisp keywords load");
    check(lispConfig.value_for_file("word.characters").value_or("").find("abc") != std::string::npos,
        "bundled Lisp word characters expand shared variables");
    check(lispConfig.value("style.lisp.1").has_value(), "bundled Lisp styles load");
    check(lispConfig.value("style.lisp.2") == std::optional<std::string>("fore:#000000"),
        "bundled Lisp numbers retain the user palette");
    check(lispConfig.value("style.lisp.3") == std::optional<std::string>("fore:#FF0000"),
        "bundled Lisp keywords retain the user palette");
    check(lispConfig.value("style.lisp.6") == std::optional<std::string>("fore:#008000"),
        "bundled Lisp strings retain the user palette");
    check(lispConfig.value("style.lisp.10") == std::optional<std::string>("fore:#0000FF"),
        "bundled Lisp operators retain the user palette");
    check(lispConfig.value_for_file("fold.fore") == std::optional<std::string>("#FFFFFF"),
        "fold symbols use the SciTE white fill");
    check(lispConfig.value_for_file("fold.back") == std::optional<std::string>("#000000"),
        "fold symbols use the SciTE black outline and sign");
    check(lispConfig.boolean_for_file("selection.always.visible", false),
        "bundled config keeps selections visible while editing");
    check(lispConfig.value_for_file("autocompleteword.automatic") ==
        std::optional<std::string>("2"),
        "bundled config preserves the original disabled word completion value");

    const SciteProperties cppConfig = SciteProperties::load_for_document(
        bundled_module, bundledConfig / L"sample.cpp", false);
    check(cppConfig.value_for_file("lexer") == std::optional<std::string>("cpp"),
        "bundled C++ lexer mapping loads");
    check(cppConfig.value_for_file("keywords").value_or("").find("constexpr") != std::string::npos,
        "bundled C++ keywords load");
    check(cppConfig.value("style.cpp.2").value_or("").find("fore:#007F00") != std::string::npos,
        "bundled C++ line comments retain the user palette");
    check(cppConfig.value("style.cpp.4") == std::optional<std::string>("fore:#000000"),
        "bundled C++ numbers retain the user palette");
    check(cppConfig.value("style.cpp.5") == std::optional<std::string>("fore:#FF0000,bold"),
        "bundled C++ keywords retain the user palette");
    check(cppConfig.value("style.cpp.6") == std::optional<std::string>("fore:#008000"),
        "bundled C++ strings retain the user palette");
    check(cppConfig.value("style.cpp.10") == std::optional<std::string>("fore:#0000FF,bold"),
        "bundled C++ operators retain the user palette");

    const SciteProperties htmlConfig = SciteProperties::load_for_document(
        bundled_module, bundledConfig / L"sample.html", false);
    check(htmlConfig.value_for_file("lexer") == std::optional<std::string>("hypertext"),
        "bundled HTML lexer mapping loads");
    check(htmlConfig.value_for_file("keywords").value_or("").find("html") != std::string::npos,
        "bundled HTML keywords load");
    check(htmlConfig.value("style.hypertext.1") == std::optional<std::string>("fore:#000080"),
        "bundled HTML tags retain their original colour");
    check(htmlConfig.value("style.hypertext.5") == std::optional<std::string>("fore:#000000"),
        "bundled HTML numbers retain the user palette");
    check(htmlConfig.value("style.hypertext.6") == std::optional<std::string>("fore:#008000"),
        "bundled HTML strings retain the user palette");

    const SciteProperties confConfig = SciteProperties::load_for_document(
        bundled_module, bundledConfig / L"sample.conf", false);
    check(confConfig.value_for_file("lexer") == std::optional<std::string>("conf"),
        "bundled configuration lexer mapping loads");
    check(confConfig.value("style.conf.1").value_or("").find("fore:#007F00") != std::string::npos,
        "bundled configuration comments retain their original colour");
    check(confConfig.value("style.conf.6").value_or("").find("fore:#7F007F") != std::string::npos,
        "bundled configuration strings retain their original colour");
    check(confConfig.value("style.conf.9") == std::optional<std::string>("fore:#00007F,bold"),
        "bundled configuration directives retain their original colour");
    check(!confConfig.value("command.build.*.conf").has_value(),
        "unsupported language commands are not bundled");

    const auto runtimeStateDirectory = std::filesystem::temp_directory_path() /
        (L"editmdview-runtime-state-" + std::to_wstring(GetCurrentProcessId()));
    const auto runtimeStateDocument = runtimeStateDirectory / L"文档.md";
    PersistedViewState savedView;
    savedView.anchor = 17;
    savedView.caret = 29;
    savedView.firstVisibleLine = 8;
    savedView.horizontalOffset = 31;
    savedView.previewScrollFraction = 0.625;
    savedView.splitRatio = 0.7;
    savedView.mode = 1;
    check(save_persisted_view_state(runtimeStateDirectory, runtimeStateDocument, savedView),
        "document view state saves atomically");
    const auto loadedView = load_persisted_view_state(runtimeStateDirectory, runtimeStateDocument);
    check(loadedView && loadedView->anchor == 17 && loadedView->caret == 29 &&
        loadedView->firstVisibleLine == 8 && loadedView->horizontalOffset == 31 &&
        std::abs(loadedView->previewScrollFraction - 0.625) < 0.000001 &&
        std::abs(loadedView->splitRatio - 0.7) < 0.000001 && loadedView->mode == 1,
        "document view state round-trips");
    const std::string recoveryText = "# 未保存\n\nrecovery content\n";
    check(save_recovery_snapshot(runtimeStateDirectory, runtimeStateDocument, recoveryText),
        "recovery snapshot saves atomically");
    const auto loadedRecovery = load_recovery_snapshot(runtimeStateDirectory, runtimeStateDocument);
    check(loadedRecovery == std::optional<std::string>(recoveryText),
        "recovery snapshot round-trips exact UTF-8 text");
    remove_recovery_snapshot(runtimeStateDirectory, runtimeStateDocument);
    check(!load_recovery_snapshot(runtimeStateDirectory, runtimeStateDocument).has_value(),
        "recovery snapshot is removed after save or discard");

    std::error_code removeError;
    std::filesystem::remove(temp, removeError);
    std::filesystem::remove(mixedEol, removeError);
    std::filesystem::remove(htmlFile, removeError);
    std::filesystem::remove(uncommonText, removeError);
    std::filesystem::remove(binaryFile, removeError);
    std::filesystem::remove(bomlessUtf16, removeError);
    std::filesystem::remove_all(propertiesDirectory, removeError);
    std::filesystem::remove_all(runtimeStateDirectory, removeError);
    return failures == 0 ? 0 : 1;
}
