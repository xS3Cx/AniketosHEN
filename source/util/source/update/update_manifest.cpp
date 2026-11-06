#include <onion/update_manifest.hpp>

#include <onion/payload_catalog_policy.hpp>
#include <onion/version.h>

#include <onion_cjson.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

namespace onion::update {
namespace {

bool printable_text(std::string_view text) {
  for (const char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7f)
      return false;
  }
  return true;
}

bool field_string(const cJSON *root, const char *key, std::string *out) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(value) || !value->valuestring)
    return false;
  *out = value->valuestring;
  return true;
}

bool host_allowed(std::string_view host) {
  return host == "github.com" || host == "objects.githubusercontent.com" ||
         host == "release-assets.githubusercontent.com";
}

std::string_view host_of(std::string_view url) {
  constexpr std::string_view kHttps = "https://";
  if (!url.starts_with(kHttps))
    return {};
  std::string_view rest = url.substr(kHttps.size());
  const std::size_t slash = rest.find('/');
  std::string_view authority =
      slash == std::string_view::npos ? rest : rest.substr(0, slash);
  const std::size_t at = authority.find('@');
  if (at != std::string_view::npos)
    authority = authority.substr(at + 1);
  const std::size_t colon = authority.find(':');
  if (colon != std::string_view::npos)
    authority = authority.substr(0, colon);
  return authority;
}

} // namespace

bool valid_channel(std::string_view channel) {
  return channel == kStableChannel || channel == kBetaChannel;
}

std::string_view manifest_url(std::string_view channel) {
  return channel == kBetaChannel ? kBetaManifestUrl : kStableManifestUrl;
}

std::string normalize_version(std::string_view version) {
  const std::string trimmed = onion::payload_catalog::trim(version);
  if (!trimmed.empty() && (trimmed.front() == 'v' || trimmed.front() == 'V'))
    return trimmed.substr(1);
  return trimmed;
}

bool valid_version(std::string_view version) {
  const std::string value = normalize_version(version);
  if (value.empty() || value.size() > kMaxVersionLength)
    return false;
  bool digit = false;
  for (const char c : value) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isdigit(uc)) {
      digit = true;
      continue;
    }
    if (std::isalpha(uc))
      continue;
    if (c != '.' && c != '_' && c != '+' && c != '-')
      return false;
  }
  return digit;
}

bool development_version(std::string_view version) {
  const std::string value = onion::payload_catalog::ascii_lower(
      normalize_version(version));
  return value.empty() || value.find("dirty") != std::string::npos ||
         value.find("unknown") != std::string::npos;
}

bool valid_sha256(std::string_view sha256) {
  return onion::payload_catalog::valid_checksum(sha256);
}

bool valid_manifest_url(std::string_view url) {
  if (!onion::payload_catalog::is_https_url(url) ||
      !onion::payload_catalog::valid_url(url) ||
      url.size() > onion::payload_catalog::kMaxUrlLength)
    return false;
  const std::string lower_host =
      onion::payload_catalog::ascii_lower(host_of(url));
  if (!host_allowed(lower_host))
    return false;
  std::string lower_url = onion::payload_catalog::ascii_lower(url);
  const std::size_t query = lower_url.find_first_of("?#");
  if (query != std::string::npos)
    lower_url.resize(query);
  return lower_url.ends_with(".elf");
}

ParseResult parse_manifest(std::string_view body) {
  ParseResult result;
  if (body.empty() || body.size() > kMaxManifestBytes) {
    result.error = body.empty() ? "protocol" : "oversized";
    return result;
  }
  onion_cjson::Root root(body.data(), body.size());
  if (!root || !cJSON_IsObject(root.get())) {
    result.error = "protocol";
    return result;
  }

  Manifest manifest;
  if (!field_string(root.get(), "version", &manifest.version) ||
      !field_string(root.get(), "tag", &manifest.tag) ||
      !field_string(root.get(), "channel", &manifest.channel) ||
      !field_string(root.get(), "published_at", &manifest.published_at) ||
      !field_string(root.get(), "url", &manifest.url) ||
      !field_string(root.get(), "sha256", &manifest.sha256) ||
      !field_string(root.get(), "summary", &manifest.summary)) {
    result.error = "protocol";
    return result;
  }

  manifest.version = normalize_version(manifest.version);
  if (!valid_version(manifest.version) ||
      normalize_version(manifest.tag) != manifest.version) {
    result.error = "version";
    return result;
  }
  if (!valid_channel(manifest.channel)) {
    result.error = "channel";
    return result;
  }
  if (manifest.published_at.empty() ||
      manifest.published_at.size() > kMaxPublishedAtLength ||
      !printable_text(manifest.published_at)) {
    result.error = "published_at";
    return result;
  }
  if (!valid_manifest_url(manifest.url)) {
    result.error = "url";
    return result;
  }
  std::string normalized_hash;
  if (!valid_sha256(manifest.sha256) ||
      !onion::payload_catalog::normalize_checksum(manifest.sha256,
                                                  &normalized_hash)) {
    result.error = "hash";
    return result;
  }
  manifest.sha256 = std::move(normalized_hash);

  const cJSON *size_value =
      cJSON_GetObjectItemCaseSensitive(root.get(), "size");
  if (!cJSON_IsNumber(size_value)) {
    result.error = "size";
    return result;
  }
  const double raw_size = size_value->valuedouble;
  if (!std::isfinite(raw_size) || raw_size < 1.0 ||
      raw_size > static_cast<double>(kMaxUpdateBytes)) {
    result.error = "size";
    return result;
  }
  const auto size = static_cast<std::uint64_t>(raw_size);
  if (static_cast<double>(size) != raw_size || size > kMaxUpdateBytes) {
    result.error = "size";
    return result;
  }
  manifest.size = size;

  if (manifest.summary.empty() ||
      manifest.summary.size() > kMaxSummaryLength ||
      !printable_text(manifest.summary)) {
    result.error = "summary";
    return result;
  }

  result.ok = true;
  result.manifest = std::move(manifest);
  return result;
}

ParseResult load_manifest_text(std::string_view body) {
  return parse_manifest(body);
}

bool manifest_newer(std::string_view installed, const Manifest &manifest) {
  if (development_version(installed) || !valid_version(manifest.version))
    return false;
  return onion::payload_catalog::compare_versions(manifest.version, "",
                                                  installed, "") > 0;
}

std::string installed_version() {
  return normalize_version(ONIONHEN_VERSION);
}

} // namespace onion::update
