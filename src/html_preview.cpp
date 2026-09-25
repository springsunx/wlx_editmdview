#include "html_preview.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace editmdview {
namespace {

std::string escape_attribute(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
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

std::size_t find_case_insensitive(std::string_view value, std::string_view needle) {
    const auto match = std::search(value.begin(), value.end(), needle.begin(), needle.end(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        });
    return match == value.end() ? std::string_view::npos : static_cast<std::size_t>(match - value.begin());
}

bool tag_name_character(unsigned char character) {
    return std::isalnum(character) != 0 || character == ':' || character == '-' || character == '_';
}

std::size_t quoted_tag_end(std::string_view value, std::size_t start) {
    char quote = '\0';
    for (std::size_t index = start; index < value.size(); ++index) {
        const char character = value[index];
        if (quote != '\0') {
            if (character == quote) quote = '\0';
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == '>') {
            return index + 1;
        }
    }
    return value.size();
}

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

std::string add_source_line_attributes(std::string_view source) {
    std::vector<std::size_t> lineStarts{0};
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\n') lineStarts.push_back(index + 1);
    }

    std::string output;
    output.reserve(source.size() + source.size() / 12);
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const std::size_t opening = source.find('<', cursor);
        if (opening == std::string_view::npos) {
            output.append(source.substr(cursor));
            break;
        }
        output.append(source.substr(cursor, opening - cursor));
        if (source.substr(opening, 4) == "<!--") {
            const std::size_t commentEnd = source.find("-->", opening + 4);
            const std::size_t end = commentEnd == std::string_view::npos ? source.size() : commentEnd + 3;
            output.append(source.substr(opening, end - opening));
            cursor = end;
            continue;
        }
        if (opening + 1 >= source.size() || source[opening + 1] == '/' ||
            source[opening + 1] == '!' || source[opening + 1] == '?') {
            const std::size_t end = quoted_tag_end(source, opening + 1);
            output.append(source.substr(opening, end - opening));
            cursor = end;
            continue;
        }

        const std::size_t nameStart = opening + 1;
        std::size_t nameEnd = nameStart;
        while (nameEnd < source.size() &&
            tag_name_character(static_cast<unsigned char>(source[nameEnd]))) ++nameEnd;
        if (nameEnd == nameStart) {
            output += '<';
            cursor = opening + 1;
            continue;
        }
        const std::size_t end = quoted_tag_end(source, nameEnd);
        const int line = static_cast<int>(
            std::upper_bound(lineStarts.begin(), lineStarts.end(), opening) - lineStarts.begin());
        output.append(source.substr(opening, nameEnd - opening));
        output += " data-editmdview-source-line=\"" + std::to_string(std::max(1, line)) + "\"";
        output.append(source.substr(nameEnd, end - nameEnd));
        cursor = end;

        const std::string tagName = lower_ascii(source.substr(nameStart, nameEnd - nameStart));
        if (tagName == "script" || tagName == "style" || tagName == "textarea" || tagName == "title") {
            const std::string closing = "</" + tagName;
            const std::size_t relative = find_case_insensitive(source.substr(cursor), closing);
            if (relative != std::string_view::npos) {
                output.append(source.substr(cursor, relative));
                cursor += relative;
            }
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

} // namespace

std::wstring render_html_preview(std::string_view source, std::string_view baseHref) {
    std::string metadata =
        "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
        "script-src 'none'; connect-src 'none'; object-src 'none'; frame-src 'none'; "
        "img-src data: https://editmdview.local; media-src data: https://editmdview.local; "
        "font-src data: https://editmdview.local; style-src 'unsafe-inline' https://editmdview.local; "
        "base-uri https://editmdview.local;\">"
        "<meta charset=\"utf-8\">";
    if (!baseHref.empty()) {
        metadata += "<base href=\"" + escape_attribute(baseHref) + "\">";
    }

    // A meta-delivered CSP only protects content that follows it. Prefix a fresh
    // standards-mode document and the restrictive policy before every byte of
    // the user document so scripts cannot run before the policy is parsed.
    std::string html = "<!doctype html>" + metadata + add_source_line_attributes(source);
    return utf8_to_wide(html);
}

} // namespace editmdview
