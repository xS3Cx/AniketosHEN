#pragma once

#include <cstdint>
#include <string>

#include <onion/payload_catalog_policy.hpp>

namespace onion::payload_center {

struct InstallRequest {
  std::string source_id;
  std::string source_url;
  std::string source_name;
  std::string item_id;
  std::string name;
  std::string filename;
  std::string url;
  std::string checksum;
  std::string version;
  std::string last_update;
  std::string category;
  std::string match_key;
  bool has_checksum = false;
  bool replace = false;
  bool from_url = false;
};

struct InstallResult {
  bool ok = false;
  std::string error;
  std::string path;
  std::string identity;
};

inline std::string validate_install_request(const InstallRequest &request) {
  if (!onion::payload_catalog::valid_url(request.url) ||
      !onion::payload_catalog::is_https_url(request.url))
    return "rejected";
  if (request.name.empty())
    return "rejected";
  if (request.has_checksum &&
      !onion::payload_catalog::valid_checksum(request.checksum))
    return "hash";
  if (request.filename.size() > onion::payload_catalog::kMaxFilename ||
      (!request.filename.empty() &&
       !onion::payload_catalog::valid_filename(request.filename)))
    return "rejected";
  return {};
}

using InstallProgressFn = void (*)(const char *phase, uint64_t completed,
                                   uint64_t total, void *user);
using InstallCancelFn = bool (*)(void *user);

InstallResult install_payload(const InstallRequest &request,
                              InstallProgressFn progress, void *progress_user,
                              InstallCancelFn cancelled, void *cancel_user);

InstallResult remove_payload_by_identity(const std::string &identity);

}
