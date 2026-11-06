#include <onion/payload_sources_json.hpp>

#include <cJSON.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace onion::payload_catalog {
namespace {

bool atomic_write(const char *path, const std::string &body) {
  if (!path || !path[0])
    return false;
  const std::string full(path);
  const std::size_t slash = full.find_last_of('/');
  if (slash != std::string::npos) {
    const std::string directory = full.substr(0, slash);
    if (!directory.empty() && mkdir(directory.c_str(), 0755) != 0 &&
        errno != EEXIST)
      return false;
  }
  std::string pattern(path);
  pattern += ".tmp.XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0)
    return false;
  std::size_t written = 0;
  while (written < body.size()) {
    const ssize_t n = write(fd, body.data() + written, body.size() - written);
    if (n <= 0) {
      close(fd);
      unlink(buffer.data());
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  if (fsync(fd) != 0) {
    close(fd);
    unlink(buffer.data());
    return false;
  }
  close(fd);
  if (chmod(buffer.data(), 0644) != 0) {
    unlink(buffer.data());
    return false;
  }
  if (rename(buffer.data(), path) != 0) {
    unlink(buffer.data());
    return false;
  }
  const std::string directory =
      std::string(path).substr(0, std::string(path).find_last_of('/') + 1);
  if (!directory.empty()) {
    const int dir_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
      fsync(dir_fd);
      close(dir_fd);
    }
  }
  return true;
}

}

SourcesFile default_sources() {
  SourcesFile out;
  out.sources.push_back({std::string(kBuiltinSourceName),
                         std::string(kBuiltinSourceUrl), true});
  return out;
}

bool parse_sources(std::string_view json, SourcesFile *out) {
  if (!out || json.empty() || json.size() > kMaxCatalogBytes)
    return false;
  const std::string text(json);
  const char *end = nullptr;
  cJSON *root = cJSON_ParseWithOpts(text.c_str(), &end, 0);
  if (root) {
    while (end && *end &&
           std::isspace(static_cast<unsigned char>(*end)))
      ++end;
    if (end && *end) {
      cJSON_Delete(root);
      root = nullptr;
    }
  }
  if (!root)
    return false;
  const cJSON *items = root;
  if (cJSON_IsObject(root))
    items = cJSON_GetObjectItemCaseSensitive(root, "sources");
  if (!cJSON_IsArray(items)) {
    cJSON_Delete(root);
    return false;
  }
  SourcesFile parsed;
  const cJSON *entry = nullptr;
  cJSON_ArrayForEach(entry, items) {
    if (!cJSON_IsObject(entry))
      continue;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(entry, "url");
    const cJSON *builtin = cJSON_GetObjectItemCaseSensitive(entry, "builtin");
    if (!cJSON_IsString(url) || !url->valuestring)
      continue;
    const std::string source_url = trim(url->valuestring);
    if (!is_https_url(source_url) || !valid_url(source_url))
      continue;
    if (source_url.size() > kMaxUrlLength)
      continue;
    Source source;
    source.url = source_url;
    source.name = cJSON_IsString(name) && name->valuestring
                      ? trim(name->valuestring)
                      : std::string{};
    if (source.name.empty())
      source.name = source_url;
    source.name = source.name.substr(0, kMaxNameLength);
    source.builtin = cJSON_IsTrue(builtin);
    bool duplicate = false;
    for (const Source &existing : parsed.sources) {
      if (existing.url == source.url) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate)
      parsed.sources.push_back(std::move(source));
  }
  cJSON_Delete(root);
  parsed.sources.erase(
      std::remove_if(parsed.sources.begin(), parsed.sources.end(),
                     [](const Source &source) { return source.builtin; }),
      parsed.sources.end());
  parsed.sources.insert(parsed.sources.begin(), default_sources().sources.front());
  for (std::size_t i = parsed.sources.size(); i > 1; --i) {
    const std::size_t index = i - 1;
    for (std::size_t j = 0; j < index; ++j) {
      if (parsed.sources[j].url == parsed.sources[index].url) {
        parsed.sources.erase(parsed.sources.begin() +
                             static_cast<std::ptrdiff_t>(index));
        break;
      }
    }
  }
  std::size_t user_count = 0;
  for (std::size_t i = 1; i < parsed.sources.size();) {
    if (++user_count <= kMaxSources) {
      ++i;
    } else {
      parsed.sources.erase(parsed.sources.begin() +
                           static_cast<std::ptrdiff_t>(i));
    }
  }
  *out = std::move(parsed);
  return true;
}

std::string serialize_sources(const SourcesFile &sources) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return {};
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON *array = cJSON_AddArrayToObject(root, "sources");
  if (!array) {
    cJSON_Delete(root);
    return {};
  }
  bool builtin_written = false;
  for (const Source &source : sources.sources) {
    if (source.builtin && builtin_written)
      continue;
    cJSON *entry = cJSON_CreateObject();
    if (!entry)
      continue;
    cJSON_AddStringToObject(entry, "name", source.name.c_str());
    cJSON_AddStringToObject(entry, "url", source.url.c_str());
    cJSON_AddBoolToObject(entry, "builtin", source.builtin);
    cJSON_AddItemToArray(array, entry);
    builtin_written = builtin_written || source.builtin;
  }
  if (!builtin_written) {
    cJSON *entry = cJSON_CreateObject();
    if (entry) {
      cJSON_AddStringToObject(entry, "name",
                              std::string(kBuiltinSourceName).c_str());
      cJSON_AddStringToObject(entry, "url",
                              std::string(kBuiltinSourceUrl).c_str());
      cJSON_AddBoolToObject(entry, "builtin", true);
      cJSON_AddItemToArray(array, entry);
    }
  }
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text ? text : "";
  if (text)
    cJSON_free(text);
  cJSON_Delete(root);
  if (!out.empty())
    out.push_back('\n');
  return out;
}

bool load_sources_file(const char *path, SourcesFile *out) {
  if (!out)
    return false;
  *out = default_sources();
  if (!path)
    return false;
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  struct stat st {};
  if (fstat(fd, &st) != 0 || st.st_size <= 0 ||
      static_cast<std::size_t>(st.st_size) > kMaxCatalogBytes) {
    close(fd);
    return false;
  }
  std::string body(static_cast<std::size_t>(st.st_size), '\0');
  std::size_t read_bytes = 0;
  while (read_bytes < body.size()) {
    const ssize_t n = read(fd, body.data() + read_bytes, body.size() - read_bytes);
    if (n <= 0)
      break;
    read_bytes += static_cast<std::size_t>(n);
  }
  close(fd);
  if (read_bytes != body.size())
    return false;
  SourcesFile parsed;
  if (!parse_sources(body, &parsed))
    return false;
  *out = std::move(parsed);
  return true;
}

bool save_sources_file(const char *path, const SourcesFile &sources) {
  return atomic_write(path, serialize_sources(sources));
}

bool add_source(SourcesFile *sources, std::string_view name, std::string_view url,
                std::string *error) {
  if (!sources) {
    if (error)
      *error = "invalid";
    return false;
  }
  const std::string clean_url = trim(url);
  if (!is_https_url(clean_url) || !valid_url(clean_url) ||
      clean_url.size() > kMaxUrlLength) {
    if (error)
      *error = "url";
    return false;
  }
  if (sources->sources.size() >= kMaxSources + 1) {
    if (error)
      *error = "limit";
    return false;
  }
  for (const Source &source : sources->sources) {
    if (source.url == clean_url) {
      if (error)
        *error = "duplicate";
      return false;
    }
  }
  std::string clean_name = trim(name);
  if (clean_name.empty())
    clean_name = clean_url;
  clean_name = clean_name.substr(0, kMaxNameLength);
  sources->sources.push_back({std::move(clean_name), clean_url, false});
  return true;
}

bool remove_source(SourcesFile *sources, std::string_view url) {
  if (!sources)
    return false;
  for (std::size_t i = 0; i < sources->sources.size(); ++i) {
    if (sources->sources[i].builtin)
      continue;
    if (sources->sources[i].url == url) {
      sources->sources.erase(sources->sources.begin() +
                             static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

}
