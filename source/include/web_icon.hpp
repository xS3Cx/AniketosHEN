/* What a website's Home tile needs from its page: the values the user gave,
 * and the page's icon link (host-testable, no network, Mono or PS5 I/O). Pages
 * come from the network, so every length is checked before it is used.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace WebIcon {

constexpr size_t kMaxHtml = 256 * 1024;
constexpr size_t kMaxIcon = 1024 * 1024;
constexpr uint32_t kMaxIconSide = 1024;
constexpr size_t kMaxUrl = 2048;
/* The tile's picture when the site offers none. */
constexpr int kTileSize = 256;

namespace detail {

inline char lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::string lowered(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), lower);
  return out;
}

inline size_t find_nocase(std::string_view text, std::string_view word,
                          size_t from) {
  for (size_t i = from; i + word.size() <= text.size(); ++i) {
    size_t k = 0;
    while (k < word.size() && lower(text[i + k]) == word[k])
      ++k;
    if (k == word.size())
      return i;
  }
  return std::string_view::npos;
}

struct LinkTag {
  std::string_view rel, href, sizes, type;
  size_t end = 0; /* just past the tag */
  bool complete = false;
};

/* The attributes of the tag whose name ends at `at`. */
inline LinkTag read_link(std::string_view html, size_t at) {
  LinkTag tag;
  while (at < html.size()) {
    while (at < html.size() && (space(html[at]) || html[at] == '/'))
      ++at;
    if (at >= html.size())
      return tag;
    if (html[at] == '>') {
      tag.end = at + 1;
      tag.complete = true;
      return tag;
    }
    const size_t name_start = at;
    while (at < html.size() && !space(html[at]) && html[at] != '=' &&
           html[at] != '>' && html[at] != '/')
      ++at;
    const std::string name =
        lowered(html.substr(name_start, at - name_start));
    while (at < html.size() && space(html[at]))
      ++at;
    std::string_view value;
    if (at < html.size() && html[at] == '=') {
      ++at;
      while (at < html.size() && space(html[at]))
        ++at;
      if (at >= html.size())
        return tag;
      if (html[at] == '"' || html[at] == '\'') {
        const size_t close = html.find(html[at], at + 1);
        if (close == std::string_view::npos)
          return tag;
        value = html.substr(at + 1, close - at - 1);
        at = close + 1;
      } else {
        const size_t value_start = at;
        while (at < html.size() && !space(html[at]) && html[at] != '>')
          ++at;
        value = html.substr(value_start, at - value_start);
      }
    }
    if (name == "rel")
      tag.rel = value;
    else if (name == "href")
      tag.href = value;
    else if (name == "sizes")
      tag.sizes = value;
    else if (name == "type")
      tag.type = value;
  }
  return tag;
}

/* The widest of "32x32 64x64"; 0 when it says nothing usable. */
inline int widest(std::string_view sizes) {
  int best = 0;
  size_t at = 0;
  while (at < sizes.size()) {
    int width = 0;
    bool digits = false;
    while (at < sizes.size() && sizes[at] >= '0' && sizes[at] <= '9') {
      width = std::min(width * 10 + (sizes[at] - '0'), 100000);
      digits = true;
      ++at;
    }
    if (digits && at < sizes.size() && lower(sizes[at]) == 'x')
      best = std::max(best, width);
    while (at < sizes.size() && !space(sizes[at]))
      ++at;
    while (at < sizes.size() && space(sizes[at]))
      ++at;
  }
  return best;
}

/* 0 for a link that is no site icon. Bigger is better for a tile. */
inline int icon_score(const LinkTag &tag) {
  bool plain = false, touch = false;
  const std::string rel = lowered(tag.rel);
  size_t at = 0;
  while (at < rel.size()) {
    size_t end = at;
    while (end < rel.size() && !space(rel[end]))
      ++end;
    const std::string_view token =
        std::string_view(rel).substr(at, end - at);
    plain = plain || token == "icon";
    touch = touch || token == "apple-touch-icon" ||
            token == "apple-touch-icon-precomposed";
    at = end + 1;
  }
  if ((!plain && !touch) || tag.href.empty())
    return 0;
  int score = widest(tag.sizes);
  if (score == 0)
    score = touch ? 180 : 32;
  /* The system's reader for it is the least proven: a raster icon first. */
  const std::string href = lowered(tag.href);
  if (lowered(tag.type) == "image/svg+xml" ||
      href.find(".svg") != std::string::npos)
    score = 1;
  return score;
}

inline void replace_all(std::string &text, const char *from, const char *to) {
  const size_t length = std::strlen(from);
  for (size_t at = text.find(from); at != std::string::npos;
       at = text.find(from, at + 1))
    text.replace(at, length, to);
}

} // namespace detail

inline bool ValidTileId(std::string_view id) {
  if (id.size() != 9 || id.substr(0, 4) != "WEBT")
    return false;
  return std::all_of(id.begin() + 4, id.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

inline bool ValidPageUrl(std::string_view url) {
  if (url.empty() || url.size() > kMaxUrl)
    return false;
  if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)
    return false;
  return std::none_of(url.begin(), url.end(), [](char c) {
    return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f;
  });
}

/* The best site icon a page names in its head, as written there. */
inline std::string FindIconHref(std::string_view html) {
  html = html.substr(0, kMaxHtml);
  const size_t head_end = detail::find_nocase(html, "</head", 0);
  if (head_end != std::string_view::npos)
    html = html.substr(0, head_end);
  std::string_view best;
  int best_score = 0;
  size_t at = 0;
  while ((at = detail::find_nocase(html, "<link", at)) !=
         std::string_view::npos) {
    at += 5;
    if (at >= html.size())
      break;
    if (!detail::space(html[at]) && html[at] != '/')
      continue;
    const detail::LinkTag tag = detail::read_link(html, at);
    if (!tag.complete)
      break;
    at = tag.end;
    const int score = detail::icon_score(tag);
    if (score > best_score) {
      best_score = score;
      best = tag.href;
    }
  }
  std::string href(best);
  detail::replace_all(href, "&amp;", "&");
  return href;
}

/* `href` against the page it came from; empty for anything but http(s). */
inline std::string ResolveUrl(const std::string &base, std::string_view href) {
  while (!href.empty() && detail::space(href.front()))
    href.remove_prefix(1);
  while (!href.empty() && detail::space(href.back()))
    href.remove_suffix(1);
  const size_t scheme = base.find("://");
  if (href.empty() || scheme == std::string::npos)
    return {};
  const size_t host_end = base.find_first_of("/?#", scheme + 3);
  const std::string origin = base.substr(0, host_end);
  std::string out;
  const std::string start = detail::lowered(href.substr(0, 8));
  const size_t colon = href.find(':');
  const size_t slash = href.find_first_of("/?#");
  if (start.compare(0, 7, "http://") == 0 ||
      start.compare(0, 8, "https://") == 0) {
    out = href;
  } else if (href.compare(0, 2, "//") == 0) {
    out = base.substr(0, scheme + 1) + std::string(href);
  } else if (colon != std::string_view::npos &&
             (slash == std::string_view::npos || colon < slash)) {
    return {}; /* data:, javascript: and the like */
  } else if (href.front() == '/') {
    out = origin + std::string(href);
  } else {
    std::string path =
        host_end == std::string::npos ? "/" : base.substr(host_end);
    path = path.substr(0, path.find_first_of("?#"));
    if (href.front() != '?')
      path = path.substr(0, path.rfind('/') + 1);
    if (path.empty())
      path = "/";
    out = origin + path + std::string(href);
  }
  return ValidPageUrl(out) ? out : std::string();
}

} // namespace WebIcon
