#pragma once

#include <string>
#include <string_view>

namespace editmdview {

std::wstring render_html_preview(std::string_view source, std::string_view baseHref);

} // namespace editmdview
