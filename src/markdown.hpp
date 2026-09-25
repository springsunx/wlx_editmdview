#pragma once

#include <string>
#include <string_view>

namespace editmdview {

struct MarkdownRenderOptions {
    bool dark = false;
    std::string baseHref;
    std::string title;
};

std::wstring render_markdown_html(std::string_view markdown, const MarkdownRenderOptions& options);

} // namespace editmdview
