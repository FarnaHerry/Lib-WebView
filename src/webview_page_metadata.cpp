#include <charconv>
#include <stdexcept>
#include <utility>

#include "detail/webview_internal.h"

namespace huxerui::detail {

// Native script evaluators return JSON. URI-encoding each field gives all of
// them the same JSON string without escapes or a platform-specific JSON parser.
const char web_view_page_metadata_script[] = R"JS((() => {
  if (!document.head || document.readyState !== 'complete') return null;
  const encode = value => {
    try {
      return encodeURIComponent(value.slice(0, 4096).replace(/[\uD800-\uDBFF]$/, ''));
    } catch (_) {
      return '';
    }
  };
  const description = document.head.querySelector('meta[name="description" i]');
  let themeColor = '';
  for (const meta of document.head.querySelectorAll('meta[name="theme-color" i]')) {
    const media = meta.getAttribute('media');
    if (meta.content.trim() && (!media || window.matchMedia(media).matches)) {
      themeColor = meta.content.trim();
      break;
    }
  }

  const fields = [encode(description ? description.content.trim() : ''), encode(themeColor)];
  const urls = new Set();
  for (const link of document.head.querySelectorAll('link[rel][href]')) {
    const rel = link.rel.toLowerCase().split(/\s+/);
    if (!rel.some(token => token === 'icon' || token === 'apple-touch-icon' ||
                           token === 'apple-touch-icon-precomposed')) continue;
    if (!link.getAttribute('href').trim() || link.href.length > 4096 || urls.has(link.href)) continue;
    urls.add(link.href);
    fields.push(encode(link.href));
    if (urls.size === 16) break;
  }
  return fields.join('|');
})())JS";

namespace {

std::optional<std::string> DecodeField(std::string_view encoded) {
  std::string value;
  value.reserve(encoded.size());
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    const char character = encoded[index];
    if (character != '%') {
      if (character < '!' || character > '~' || character == '"' || character == '\\') return std::nullopt;
      value.push_back(character);
      continue;
    }
    if (encoded.size() - index < 3) return std::nullopt;
    const char* end = encoded.data() + index + 3;
    unsigned int byte = 0;
    const auto [position, error] = std::from_chars(end - 2, end, byte, 16);
    if (error != std::errc{} || position != end || byte == 0) return std::nullopt;
    value.push_back(static_cast<char>(byte));
    index += 2;
  }
  try {
    return std::string(PlatformPayload(std::move(value)).AsString());
  } catch (const std::invalid_argument&) {
    return std::nullopt;
  }
}

} // namespace

WebViewPageMetadata DecodePageMetadata(std::int64_t document_id, std::string_view result) {
  WebViewPageMetadata empty{.document_id = document_id};
  constexpr std::size_t max_fields = 18;
  constexpr std::size_t max_field_length = 4096 * 9;
  if (result.size() < 3 || result.size() > max_fields * (max_field_length + 1) + 1 ||
      result.front() != '"' || result.back() != '"') {
    return empty;
  }
  result.remove_prefix(1);
  result.remove_suffix(1);

  WebViewPageMetadata metadata = empty;
  for (std::size_t index = 0; index < max_fields; ++index) {
    const auto separator = result.find('|');
    const auto encoded = result.substr(0, separator);
    if (encoded.size() > max_field_length) return empty;
    auto field = DecodeField(encoded);
    if (!field) return empty;
    if (!field->empty()) {
      if (index == 0) {
        metadata.description = std::move(*field);
      } else if (index == 1) {
        metadata.theme_color = std::move(*field);
      } else {
        metadata.icon_urls.push_back(std::move(*field));
      }
    }
    if (separator == std::string_view::npos) return index >= 1 ? metadata : empty;
    result.remove_prefix(separator + 1);
  }
  return empty;
}

} // namespace huxerui::detail

namespace huxerui {

WebViewPageMetadata WebViewPageMetadata::Decode(const PlatformPayload& payload) {
  const auto& fields = payload.AsObject();
  const auto& result = fields.at("result");
  return detail::DecodePageMetadata(
      fields.at("documentId").AsInteger(), result.IsNull() ? std::string_view{} : result.AsString());
}

} // namespace huxerui
