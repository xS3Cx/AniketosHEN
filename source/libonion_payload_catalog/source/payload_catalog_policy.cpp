#include <onion/payload_catalog_policy.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <vector>

namespace onion::payload_catalog {
namespace {

bool is_separator(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isspace(uc) || c == '-' || c == '_' || c == '.' || c == '+' ||
         c == '/' || c == ':' || c == '~';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::vector<std::string> version_tokens(std::string_view text) {
  std::string value = trim(text);
  if (!value.empty() && (value.front() == 'v' || value.front() == 'V'))
    value.erase(value.begin());
  std::vector<std::string> tokens;
  std::size_t at = 0;
  while (at < value.size()) {
    if (is_separator(value[at])) {
      ++at;
      continue;
    }
    const std::size_t begin = at;
    const bool numeric = is_digit(value[at]);
    while (at < value.size() &&
           ((numeric && is_digit(value[at])) ||
            (!numeric && is_alpha(value[at])))) {
      ++at;
    }
    if (at == begin)
      ++at;
    else
      tokens.emplace_back(value.substr(begin, at - begin));
  }
  return tokens;
}

int numeric_compare(const std::string &first, const std::string &second) {
  std::size_t a = first.find_first_not_of('0');
  std::size_t b = second.find_first_not_of('0');
  const std::string_view av =
      a == std::string::npos ? std::string_view{} : std::string_view(first).substr(a);
  const std::string_view bv = b == std::string::npos
                                  ? std::string_view{}
                                  : std::string_view(second).substr(b);
  if (av.size() != bv.size())
    return av.size() < bv.size() ? -1 : 1;
  if (av < bv)
    return -1;
  if (av > bv)
    return 1;
  return 0;
}

int alpha_compare(const std::string &first, const std::string &second) {
  const std::string a = ascii_lower(first);
  const std::string b = ascii_lower(second);
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

bool date_key(std::string_view value, std::string *out) {
  std::string digits;
  for (const char c : value) {
    if (is_digit(c))
      digits.push_back(c);
  }
  if (digits.size() < 8)
    return false;
  digits.resize(8);
  *out = std::move(digits);
  return true;
}

}

std::string trim(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])))
    ++first;
  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1])))
    --last;
  return std::string(text.substr(first, last - first));
}

std::string ascii_lower(std::string_view text) {
  std::string out(text);
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

uint32_t fnv1a32(std::string_view text) {
  uint32_t hash = 2166136261u;
  for (const char c : text) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

uint64_t fnv1a64(std::string_view text) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : text) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string hex32(uint32_t value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(8, '0');
  for (int i = 7; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0xfu];
    value >>= 4;
  }
  return out;
}

std::string hex64(uint64_t value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0xfu];
    value >>= 4;
  }
  return out;
}

std::string source_hash8(std::string_view url) { return hex32(fnv1a32(url)); }

std::string icon_cache_name(std::string_view icon_key) {
  return hex64(fnv1a64(icon_key));
}

bool is_https_url(std::string_view url) {
  return url.size() > 8 && url.substr(0, 8) == "https://";
}

bool is_http_url(std::string_view url) {
  return url.size() > 7 && url.substr(0, 7) == "http://";
}

bool valid_url(std::string_view url) {
  if (!is_https_url(url) && !is_http_url(url))
    return false;
  if (url.size() > kMaxUrlLength)
    return false;
  const std::size_t scheme = url.find("://");
  const std::string_view host = url.substr(scheme + 3);
  if (host.empty())
    return false;
  const std::size_t end = host.find_first_of("/?#");
  const std::string_view authority = host.substr(0, end);
  if (authority.empty() || authority.find(' ') != std::string_view::npos)
    return false;
  for (const char c : url) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x21 || uc == 0x7f)
      return false;
  }
  return true;
}

bool valid_filename(std::string_view filename) {
  if (filename.empty() || filename.size() > kMaxNameLength)
    return false;
  if (filename == "." || filename == "..")
    return false;
  for (const char c : filename) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7f || c == '/' || c == '\\' || c == ':')
      return false;
  }
  return true;
}

bool is_binary_filename(std::string_view filename) {
  return ascii_lower(filename).ends_with(".bin");
}

bool valid_checksum(std::string_view checksum) {
  std::string normalized;
  return normalize_checksum(checksum, &normalized);
}

bool normalize_checksum(std::string_view checksum, std::string *out) {
  std::string value = ascii_lower(trim(checksum));
  if (value.starts_with("sha256:"))
    value.erase(0, 7);
  else if (value.starts_with("sha256="))
    value.erase(0, 7);
  if (value.size() != 64)
    return false;
  for (const char c : value) {
    if (!is_digit(c) && !(c >= 'a' && c <= 'f'))
      return false;
  }
  if (out)
    *out = std::move(value);
  return true;
}

bool has_elf_header(const void *data, std::size_t size) {
  if (!data || size < 16)
    return false;
  const auto *bytes = static_cast<const uint8_t *>(data);
  return bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' &&
         bytes[3] == 'F' && bytes[4] == 2 && bytes[5] == 1;
}

std::string slug(std::string_view name) {
  std::string out;
  bool separator = false;
  for (const char c : ascii_lower(name)) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      if (separator && !out.empty())
        out.push_back('-');
      out.push_back(static_cast<char>(uc));
      separator = false;
    } else {
      separator = true;
    }
  }
  while (!out.empty() && out.back() == '-')
    out.pop_back();
  if (out.empty())
    out = "payload";
  if (out.size() > 80)
    out.resize(80);
  return out;
}

int version_compare(std::string_view first, std::string_view second) {
  const std::vector<std::string> a = version_tokens(first);
  const std::vector<std::string> b = version_tokens(second);
  const std::size_t count = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < count; ++i) {
    const bool an = !a[i].empty() && is_digit(a[i][0]);
    const bool bn = !b[i].empty() && is_digit(b[i][0]);
    if (an && bn) {
      const int cmp = numeric_compare(a[i], b[i]);
      if (cmp != 0)
        return cmp;
    } else if (!an && !bn) {
      const int cmp = alpha_compare(a[i], b[i]);
      if (cmp != 0)
        return cmp;
    } else {
      return an ? 1 : -1;
    }
  }
  if (a.size() == b.size())
    return 0;
  const bool first_remains = a.size() > b.size();
  const std::vector<std::string> &remaining = first_remains ? a : b;
  const bool numeric = is_digit(remaining[count][0]);
  if (!numeric)
    return first_remains ? -1 : 1;
  return first_remains ? 1 : -1;
}

int compare_versions(std::string_view first, std::string_view first_update,
                     std::string_view second, std::string_view second_update) {
  const int cmp = version_compare(first, second);
  if (cmp != 0)
    return cmp;
  std::string a;
  std::string b;
  const bool have_a = date_key(first_update, &a);
  const bool have_b = date_key(second_update, &b);
  if (have_a && have_b) {
    if (a < b)
      return -1;
    if (a > b)
      return 1;
  }
  return 0;
}

bool is_system_or_conflicting_name(std::string_view name) {
  const std::string value = ascii_lower(name);
  static constexpr std::array<std::string_view, 9> kNames = {
      "elfldr", "kstuff", "etahen", "pldmgr", "lapy-jb", "backpork",
      "ps5-linux-loader", "onionhen", "aniketoshen"};
  for (const auto known : kNames) {
    if (known == "kstuff") {
      if (value.starts_with("kstuff"))
        return true;
      continue;
    }
    if (value.find(known) != std::string::npos)
      return true;
  }
  return false;
}

bool autostart_allowed(std::string_view filename) {
  return ascii_lower(filename).find("elfldr") == std::string::npos;
}

}
