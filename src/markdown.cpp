#include "markdown.hpp"
#include "i18n.hpp"
#include <windows.h>

#include <md4c.h>
#include <md4c-html.h>
extern "C" {
#include <entity.h>
}

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editmdview {
namespace {

void append_output(const MD_CHAR* text, MD_SIZE size, void* userData) {
    static_cast<std::string*>(userData)->append(text, size);
}

std::string escape_attribute(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (char character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output += character; break;
        }
    }
    return output;
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
        required, nullptr, nullptr);
    return result;
}

void append_utf8_codepoint(std::string& output, unsigned codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        output += "\xEF\xBF\xBD";
    } else if (codepoint <= 0x7F) {
        output += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        output += static_cast<char>(0xC0 | (codepoint >> 6));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        output += static_cast<char>(0xE0 | (codepoint >> 12));
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        output += static_cast<char>(0xF0 | (codepoint >> 18));
        output += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

void append_entity_text(std::string& output, const char* text, std::size_t size) {
    unsigned codepoint = 0;
    if (size > 3 && text[1] == '#') {
        const bool hexadecimal = text[2] == 'x' || text[2] == 'X';
        const std::size_t start = hexadecimal ? 3 : 2;
        for (std::size_t index = start; index + 1 < size; ++index) {
            const unsigned char character = static_cast<unsigned char>(text[index]);
            unsigned digit = 0;
            if (character >= '0' && character <= '9') digit = character - '0';
            else if (hexadecimal && character >= 'a' && character <= 'f') digit = character - 'a' + 10;
            else if (hexadecimal && character >= 'A' && character <= 'F') digit = character - 'A' + 10;
            else {
                output.append(text, size);
                return;
            }
            codepoint = codepoint * (hexadecimal ? 16u : 10u) + digit;
        }
        append_utf8_codepoint(output, codepoint);
        return;
    }
    if (const ENTITY* entity = entity_lookup(text, size)) {
        append_utf8_codepoint(output, entity->codepoints[0]);
        if (entity->codepoints[1] != 0) append_utf8_codepoint(output, entity->codepoints[1]);
        return;
    }
    output.append(text, size);
}

bool is_rendered_block(MD_BLOCKTYPE type) {
    return type != MD_BLOCK_DOC && type != MD_BLOCK_HTML;
}

struct SourceMapContext {
    struct Heading {
        unsigned level = 1;
        std::string text;
        std::string id;
    };

    std::string_view source;
    std::vector<std::size_t> lineStarts{0};
    std::vector<int> blockLines;
    std::vector<std::size_t> openBlocks;
    std::vector<Heading> headings;
    std::size_t currentHeading = static_cast<std::size_t>(-1);
    int lastLine = 1;
};

int source_map_enter_block(MD_BLOCKTYPE type, void* detail, void* userData) {
    auto& context = *static_cast<SourceMapContext*>(userData);
    if (is_rendered_block(type)) {
        context.openBlocks.push_back(context.blockLines.size());
        context.blockLines.push_back(0);
    }
    if (type == MD_BLOCK_H && detail) {
        context.currentHeading = context.headings.size();
        context.headings.push_back(SourceMapContext::Heading{
            static_cast<MD_BLOCK_H_DETAIL*>(detail)->level, {}, {}});
    }
    return 0;
}

int source_map_leave_block(MD_BLOCKTYPE type, void*, void* userData) {
    auto& context = *static_cast<SourceMapContext*>(userData);
    if (is_rendered_block(type) && !context.openBlocks.empty()) {
        const std::size_t index = context.openBlocks.back();
        if (context.blockLines[index] == 0) context.blockLines[index] = context.lastLine;
        context.openBlocks.pop_back();
    }
    if (type == MD_BLOCK_H) context.currentHeading = static_cast<std::size_t>(-1);
    return 0;
}

int source_map_span(MD_SPANTYPE, void*, void*) {
    return 0;
}

int source_map_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userData) {
    auto& context = *static_cast<SourceMapContext*>(userData);
    const auto begin = reinterpret_cast<std::uintptr_t>(context.source.data());
    const auto end = begin + context.source.size();
    const auto address = reinterpret_cast<std::uintptr_t>(text);
    if (!text || address < begin || address > end) return 0;
    const std::size_t offset = static_cast<std::size_t>(address - begin);
    const int line = static_cast<int>(
        std::upper_bound(context.lineStarts.begin(), context.lineStarts.end(), offset) -
        context.lineStarts.begin());
    context.lastLine = std::max(1, line);
    for (const std::size_t block : context.openBlocks) {
        if (context.blockLines[block] == 0) context.blockLines[block] = context.lastLine;
    }
    if (context.currentHeading < context.headings.size()) {
        auto& currentHeading = context.headings[context.currentHeading];
        std::string& heading = currentHeading.text;
        switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_LATEXMATH:
            heading.append(text, size);
            break;
        case MD_TEXT_ENTITY:
            append_entity_text(heading, text, size);
            break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            heading += ' ';
            break;
        case MD_TEXT_NULLCHAR:
            heading += "\xEF\xBF\xBD";
            break;
        case MD_TEXT_HTML:
            break;
        }
    }
    return 0;
}

SourceMapContext analyze_markdown(std::string_view markdown, unsigned parserFlags) {
    SourceMapContext context;
    context.source = markdown;
    for (std::size_t index = 0; index < markdown.size(); ++index) {
        if (markdown[index] == '\n') context.lineStarts.push_back(index + 1);
    }
    MD_PARSER parser{};
    parser.flags = parserFlags;
    parser.enter_block = source_map_enter_block;
    parser.leave_block = source_map_leave_block;
    parser.enter_span = source_map_span;
    parser.leave_span = source_map_span;
    parser.text = source_map_text;
    if (md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &context) != 0) {
        context.blockLines.clear();
        context.headings.clear();
        return context;
    }
    int previous = 1;
    for (int& line : context.blockLines) {
        if (line <= 0) line = previous;
        previous = line;
    }
    return context;
}

std::string heading_slug(std::string_view text) {
    std::wstring value = utf8_to_wide(text);
    if (!value.empty()) {
        const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
        if (required > 0) {
            std::wstring lowered(static_cast<std::size_t>(required), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(),
                    static_cast<int>(value.size()), lowered.data(), required, nullptr, nullptr, 0) > 0) {
                value = std::move(lowered);
            }
        }
    }

    std::wstring slug;
    bool pendingSeparator = false;
    for (const wchar_t character : value) {
        WORD kind = 0;
        GetStringTypeW(CT_CTYPE1, &character, 1, &kind);
        if ((kind & (C1_SPACE | C1_BLANK)) != 0) {
            pendingSeparator = !slug.empty();
            continue;
        }
        const bool asciiWord = (character >= L'a' && character <= L'z') ||
            (character >= L'0' && character <= L'9') || character == L'_' || character == L'-';
        const bool unicodeWord = character >= 0x80 && (kind & (C1_PUNCT | C1_CNTRL)) == 0;
        if (!asciiWord && !unicodeWord) continue;
        if (pendingSeparator && slug.back() != L'-' && character != L'-') slug += L'-';
        slug += character;
        pendingSeparator = false;
    }
    while (!slug.empty() && slug.back() == L'-') slug.pop_back();
    return slug.empty() ? "section" : wide_to_utf8(slug);
}

void assign_heading_ids(std::vector<SourceMapContext::Heading>& headings) {
    std::unordered_set<std::string> used;
    std::unordered_map<std::string, unsigned> suffixes;
    for (auto& heading : headings) {
        const std::string base = heading_slug(heading.text);
        std::string candidate = base;
        unsigned& suffix = suffixes[base];
        while (used.contains(candidate)) candidate = base + '-' + std::to_string(++suffix);
        used.insert(candidate);
        heading.id = std::move(candidate);
    }
}

void add_heading_ids(std::string& body, const std::vector<SourceMapContext::Heading>& headings) {
    std::size_t cursor = 0;
    for (const auto& heading : headings) {
        const std::string marker = "<h" + std::to_string(heading.level) + ">";
        const std::size_t position = body.find(marker, cursor);
        if (position == std::string::npos) continue;
        const std::string replacement = "<h" + std::to_string(heading.level) +
            " id=\"" + escape_attribute(heading.id) + "\">";
        body.replace(position, marker.size(), replacement);
        cursor = position + replacement.size();
    }
}

std::string render_outline(const std::vector<SourceMapContext::Heading>& headings) {
    if (headings.size() < 2) return {};
    unsigned minimumLevel = 6;
    for (const auto& heading : headings) minimumLevel = std::min(minimumLevel, heading.level);
    const std::size_t naturalHeight = std::min<std::size_t>(420, headings.size() * 10 + 2);
    std::string outline =
        "<nav class=\"editmdview-outline\" aria-label=\"" +
        escape_attribute(i18n::text_utf8("Document outline")) + "\" style=\"--outline-height:" +
        std::to_string(naturalHeight) + "px\">";
    for (const auto& heading : headings) {
        const unsigned depth = std::min(6u, heading.level - minimumLevel + 1);
        const std::string label = heading.text.empty() ? i18n::text_utf8("Untitled heading") : heading.text;
        outline += "<a class=\"outline-depth-" + std::to_string(depth) + "\" href=\"#" +
            escape_attribute(heading.id) + "\">"
            "<span class=\"outline-mark\" aria-hidden=\"true\"></span>"
            "<span class=\"outline-label\">" + escape_attribute(label) + "</span></a>";
    }
    outline += "</nav>";
    return outline;
}

std::string enhance_highlight_spans(std::string_view html) {
    std::string output;
    output.reserve(html.size() + 64);
    bool inCode = false;
    std::size_t position = 0;
    while (position < html.size()) {
        if (html[position] == '<') {
            const std::size_t end = html.find('>', position);
            if (end == std::string_view::npos) {
                output.append(html.substr(position));
                break;
            }
            const std::string_view tag = html.substr(position, end - position + 1);
            if (tag.starts_with("<code") || tag.starts_with("<pre")) inCode = true;
            if (tag.starts_with("</code") || tag.starts_with("</pre")) inCode = false;
            output.append(tag);
            position = end + 1;
            continue;
        }
        const std::size_t end = html.find('<', position);
        const std::string_view text = html.substr(position,
            end == std::string_view::npos ? html.size() - position : end - position);
        if (inCode) {
            output.append(text);
        } else {
            std::size_t cursor = 0;
            while (cursor < text.size()) {
                const std::size_t opening = text.find("==", cursor);
                if (opening == std::string_view::npos || opening + 2 >= text.size() ||
                    std::isspace(static_cast<unsigned char>(text[opening + 2]))) {
                    output.append(text.substr(cursor));
                    break;
                }
                const std::size_t closing = text.find("==", opening + 2);
                if (closing == std::string_view::npos || closing == opening + 2 ||
                    std::isspace(static_cast<unsigned char>(text[closing - 1]))) {
                    output.append(text.substr(cursor));
                    break;
                }
                output.append(text.substr(cursor, opening - cursor));
                output += "<mark>";
                output.append(text.substr(opening + 2, closing - opening - 2));
                output += "</mark>";
                cursor = closing + 2;
            }
        }
        if (end == std::string_view::npos) break;
        position = end;
    }
    return output;
}

std::size_t find_ascii_case_insensitive(std::string_view text, std::string_view needle,
    std::size_t start = 0) {
    if (needle.empty() || needle.size() > text.size()) return std::string_view::npos;
    for (std::size_t position = start; position + needle.size() <= text.size(); ++position) {
        bool equal = true;
        for (std::size_t index = 0; index < needle.size(); ++index) {
            const auto left = static_cast<unsigned char>(text[position + index]);
            const auto right = static_cast<unsigned char>(needle[index]);
            if (std::tolower(left) != std::tolower(right)) {
                equal = false;
                break;
            }
        }
        if (equal) return position;
    }
    return std::string_view::npos;
}

void enhance_callout(std::string& html, std::string_view name, std::string_view label) {
    const std::string marker = "[!" + std::string(name) + "]";
    std::size_t searchFrom = 0;
    while (true) {
        std::size_t markerPosition = find_ascii_case_insensitive(html, marker, searchFrom);
        if (markerPosition == std::string::npos) break;
        const std::size_t blockquote = html.rfind("<blockquote>", markerPosition);
        const std::size_t closedBlockquote = html.rfind("</blockquote>", markerPosition);
        const std::size_t paragraph = html.rfind("<p>", markerPosition);
        if (blockquote == std::string::npos || paragraph == std::string::npos ||
            paragraph < blockquote || (closedBlockquote != std::string::npos && closedBlockquote > blockquote)) {
            searchFrom = markerPosition + marker.size();
            continue;
        }
        const std::string opening = "<blockquote class=\"editmdview-callout editmdview-callout-" +
            std::string(name) + "\">";
        html.replace(blockquote, std::string_view("<blockquote>").size(), opening);
        markerPosition += opening.size() - std::string_view("<blockquote>").size();
        const std::string title = "<span class=\"editmdview-callout-title\">" +
            std::string(label) + "</span>";
        html.replace(markerPosition, marker.size(), title);
        searchFrom = markerPosition + title.size();
    }
}

void enhance_markdown_extensions(std::string& html) {
    html = enhance_highlight_spans(html);
    enhance_callout(html, "note", "ⓘ " + i18n::text_utf8("Note"));
    enhance_callout(html, "important", "◆ " + i18n::text_utf8("Important"));
    enhance_callout(html, "tip", "✦ " + i18n::text_utf8("Tip"));
    enhance_callout(html, "warning", "△ " + i18n::text_utf8("Warning"));
    enhance_callout(html, "caution", "! " + i18n::text_utf8("Caution"));
}

} // namespace

std::wstring render_markdown_html(std::string_view markdown, const MarkdownRenderOptions& options) {
    std::string body;
    const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS;
    SourceMapContext analysis = analyze_markdown(markdown, parserFlags);
    assign_heading_ids(analysis.headings);
    const int result = md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), append_output,
        &body, parserFlags, 0);
    if (result != 0) {
        body = "<p class=\"error\">Markdown rendering failed.</p>";
    } else {
        enhance_markdown_extensions(body);
        add_heading_ids(body, analysis.headings);
    }

    const char* lightColors =
        "--bg:#ffffff;--fg:#24292f;--muted:#57606a;--border:#d0d7de;--panel:#f6f8fa;"
        "--accent:#0969da;--quote:#656d76;--code:#1f2328;";
    const char* darkColors =
        "--bg:#0d1117;--fg:#e6edf3;--muted:#8b949e;--border:#30363d;--panel:#161b22;"
        "--accent:#58a6ff;--quote:#8b949e;--code:#e6edf3;";

    std::string html;
    html.reserve(body.size() + 5000);
    html += "<!doctype html><html><head><meta charset=\"utf-8\">";
    html += "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
        "script-src 'none'; connect-src 'none'; object-src 'none'; frame-src 'none'; "
        "img-src data: https://editmdview.local; media-src data: https://editmdview.local; "
        "font-src data: https://editmdview.local; style-src 'unsafe-inline' https://editmdview.local; "
        "base-uri https://editmdview.local;\">";
    html += "<meta name=\"editmdview-source-lines\" content=\"";
    for (std::size_t index = 0; index < analysis.blockLines.size(); ++index) {
        if (index != 0) html += ',';
        html += std::to_string(analysis.blockLines[index]);
    }
    html += "\">";
    if (!options.baseHref.empty()) {
        html += "<base href=\"" + escape_attribute(options.baseHref) + "\">";
    }
    html += "<title>" + escape_attribute(options.title) + "</title><style>:root{";
    html += options.dark ? darkColors : lightColors;
    html += R"css(;--editmdview-content-width:980px}*{box-sizing:border-box}html{background:var(--bg);color:var(--fg);font:16px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei UI",sans-serif;scroll-behavior:smooth}body{margin:0;overflow-wrap:anywhere}.editmdview-content{width:min(var(--editmdview-content-width),100%);margin:0 auto;padding:32px 44px 72px}h1,h2,h3,h4,h5,h6{line-height:1.28;margin:1.45em 0 .55em;font-weight:650;scroll-margin-top:24px}h1,h2{padding-bottom:.3em;border-bottom:1px solid var(--border)}h1{font-size:2em}h2{font-size:1.5em}h3{font-size:1.25em}h1:target,h2:target,h3:target,h4:target,h5:target,h6:target{background:color-mix(in srgb,var(--accent) 12%,transparent);border-radius:6px;outline:6px solid color-mix(in srgb,var(--accent) 12%,transparent)}p,ul,ol,blockquote,pre,table{margin:0 0 1em}a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}blockquote{margin-left:0;padding:.15em 1em;color:var(--quote);border-left:.25em solid var(--border)}mark{padding:.08em .2em;color:inherit;background:#fff2a8;border-radius:3px}.editmdview-callout{padding:.7em 1em .7em 1.05em;color:var(--fg);background:color-mix(in srgb,var(--panel) 72%,transparent);border:1px solid var(--border);border-left:4px solid var(--accent);border-radius:7px}.editmdview-callout p{margin:.25em 0}.editmdview-callout-title{display:block;margin-bottom:.3em;color:var(--accent);font-weight:650}.editmdview-callout-important{border-left-color:#8250df}.editmdview-callout-important .editmdview-callout-title{color:#8250df}.editmdview-callout-tip{border-left-color:#1a7f37}.editmdview-callout-tip .editmdview-callout-title{color:#1a7f37}.editmdview-callout-warning{border-left-color:#bf8700}.editmdview-callout-warning .editmdview-callout-title{color:#9a6700}.editmdview-callout-caution{border-left-color:#cf222e}.editmdview-callout-caution .editmdview-callout-title{color:#cf222e}code{font:85%/1.5 "Cascadia Code","Cascadia Mono",Consolas,monospace;background:var(--panel);border-radius:5px;padding:.16em .38em;color:var(--code)}pre{overflow:auto;padding:16px;background:var(--panel);border:1px solid var(--border);border-radius:8px}pre code{font-size:13.5px;background:transparent;padding:0}table{width:max-content;max-width:100%;border-spacing:0;border-collapse:collapse;display:block;overflow:auto}th,td{padding:6px 13px;border:1px solid var(--border)}tr:nth-child(2n){background:var(--panel)}img{max-width:100%;height:auto}hr{height:1px;border:0;background:var(--border)}input[type=checkbox]{margin-right:.5em}.editmdview-toc{position:fixed;z-index:20;top:14px;right:18px;font-size:14px}.editmdview-toc summary{display:block;cursor:pointer;user-select:none;margin-left:auto;width:max-content;padding:5px 11px;border:1px solid var(--border);border-radius:8px;background:var(--panel);color:var(--fg);box-shadow:0 2px 10px #0002}.editmdview-toc summary::-webkit-details-marker{display:none}.editmdview-toc[open]{width:min(330px,calc(100vw - 32px));max-height:min(72vh,620px);padding:10px;border:1px solid var(--border);border-radius:10px;background:var(--bg);box-shadow:0 10px 35px #0003}.editmdview-toc[open] summary{margin-bottom:7px;box-shadow:none}.editmdview-toc nav{max-height:calc(min(72vh,620px) - 48px);overflow:auto}.editmdview-toc a{display:block;padding:4px 8px;border-radius:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.editmdview-toc a:hover{background:var(--panel);text-decoration:none}.toc-depth-2{padding-left:20px!important}.toc-depth-3{padding-left:32px!important}.toc-depth-4{padding-left:44px!important}.toc-depth-5{padding-left:56px!important}.toc-depth-6{padding-left:68px!important}.error{padding:12px;border:1px solid #f85149;color:#f85149;border-radius:6px}@media(max-width:700px){.editmdview-content{padding:22px 24px 56px}.editmdview-toc{right:10px;top:10px}})css";
    html += R"css(.editmdview-outline{position:fixed;z-index:20;right:6px;top:50%;display:flex;flex-direction:column;justify-content:space-evenly;width:28px;height:min(var(--outline-height),calc(100vh - 48px),420px);padding:1px 0;pointer-events:none;transform:translateY(-50%)}.editmdview-outline a{position:relative;z-index:1;display:flex;flex:1 1 10px;align-items:center;justify-content:flex-end;width:28px;min-height:8px;max-height:24px;color:var(--muted);text-decoration:none;pointer-events:auto;outline:none}.outline-mark{display:block;width:8px;height:2px;border-radius:2px;background:currentColor;opacity:.42;transform-origin:right center;transition:width .14s ease,opacity .14s ease,color .14s ease}.outline-depth-1 .outline-mark{width:12px;opacity:.68}.outline-depth-2 .outline-mark{width:10px;opacity:.54}.outline-depth-4 .outline-mark,.outline-depth-5 .outline-mark,.outline-depth-6 .outline-mark{width:6px;opacity:.34}.editmdview-outline a:hover{z-index:4}.editmdview-outline a:hover .outline-mark,.editmdview-outline a:focus-visible .outline-mark{width:18px;color:var(--accent);opacity:1}.editmdview-outline a.current .outline-mark{width:20px;color:var(--fg);opacity:1}.editmdview-outline a.current:hover .outline-mark,.editmdview-outline a.current:focus-visible .outline-mark{width:20px;color:var(--accent)}.outline-label{position:absolute;z-index:2;right:38px;top:50%;max-width:min(320px,calc(100vw - 58px));padding:9px 12px;overflow:hidden;color:var(--fg);background:color-mix(in srgb,var(--bg) 94%,transparent);border:1px solid color-mix(in srgb,var(--border) 78%,transparent);border-radius:10px;box-shadow:0 9px 28px #0002,0 2px 7px #0001;backdrop-filter:blur(14px) saturate(1.12);font-size:13px;font-weight:550;line-height:1.35;white-space:nowrap;text-overflow:ellipsis;opacity:0;visibility:hidden;transform:translate(7px,-50%) scale(.98);transform-origin:right center;transition:opacity .12s ease,transform .18s cubic-bezier(.2,.8,.2,1),visibility 0s linear .1s;pointer-events:none}.editmdview-outline a:hover .outline-label,.editmdview-outline a:focus-visible .outline-label{opacity:1;visibility:visible;transform:translate(0,-50%) scale(1);transition-delay:.025s,0s,0s}.editmdview-outline a:hover,.editmdview-outline a:focus-visible{text-decoration:none}.editmdview-width-handle{position:fixed;z-index:18;top:0;bottom:0;width:40px;cursor:col-resize;touch-action:none}.editmdview-width-handle[data-side="left"]{right:calc(50% + min(var(--editmdview-content-width),100vw) / 2)}.editmdview-width-handle[data-side="right"]{left:calc(50% + min(var(--editmdview-content-width),100vw) / 2)}.editmdview-width-handle::after{content:"";position:absolute;top:0;bottom:0;width:3px;border-radius:3px;background:linear-gradient(to bottom,transparent calc(var(--editmdview-handle-y,50%) - 52px),color-mix(in srgb,var(--muted) 38%,transparent) calc(var(--editmdview-handle-y,50%) - 12px),color-mix(in srgb,var(--muted) 38%,transparent) calc(var(--editmdview-handle-y,50%) + 12px),transparent calc(var(--editmdview-handle-y,50%) + 52px));opacity:0;pointer-events:none;transition:opacity .14s ease}.editmdview-width-handle[data-side="left"]::after{right:16px}.editmdview-width-handle[data-side="right"]::after{left:16px}.editmdview-width-handle:hover::after,.editmdview-width-handle[data-dragging]::after{opacity:1}html.editmdview-width-dragging,html.editmdview-width-dragging *{cursor:col-resize!important;user-select:none!important}@media(max-width:540px){.editmdview-outline{right:1px}.outline-label{right:34px;max-width:calc(100vw - 42px)}.editmdview-width-handle{display:none}}@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}.outline-mark,.outline-label,.editmdview-width-handle::after{transition:none}})css";
    html += "</style></head><body>";
    html += "<div class=\"editmdview-width-handle\" data-side=\"left\" aria-hidden=\"true\"></div>";
    html += "<div class=\"editmdview-width-handle\" data-side=\"right\" aria-hidden=\"true\"></div>";
    html += render_outline(analysis.headings);
    html += "<main class=\"editmdview-content\">";
    html += body;
    html += "</main></body></html>";
    return utf8_to_wide(html);
}

} // namespace editmdview
