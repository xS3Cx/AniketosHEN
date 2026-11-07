/* Copyright (C) 2026 AniketosHEN */

#include <onion/plugin_folder.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::pluginloader {
namespace {

constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxPlugins = 128; /* per folder */

std::string_view trim(std::string_view value) noexcept {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}

bool ends_with_ci(std::string_view text, std::string_view suffix) noexcept {
  if (text.size() <= suffix.size())
    return false;
  text = text.substr(text.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(text[i])) != suffix[i])
      return false;
  return true;
}

/* key=value lines; '#'/';' comments. */
template <typename Fn> void for_each_line(std::string_view text, Fn &&fn) {
  while (!text.empty()) {
    const size_t newline = text.find('\n');
    const std::string_view line = trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view{}
                                             : text.substr(newline + 1);
    const size_t equal = line.find('=');
    if (line.empty() || line.front() == '#' || line.front() == ';' ||
        equal == std::string_view::npos)
      continue;
    fn(trim(line.substr(0, equal)), trim(line.substr(equal + 1)));
  }
}

std::string read_small(const std::string &path) {
  std::string text;
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0 &&
         text.size() < kMaxFileBytes)
    text.append(buffer, got);
  std::fclose(file);
  return text;
}

/* Written beside and renamed over, so a reader never sees half a file. */
bool write_atomic(const std::string &path, const std::string &text) {
  const std::string temporary = path + ".writing";
  FILE *file = std::fopen(temporary.c_str(), "wb");
  if (!file)
    return false;
  const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  if (std::fclose(file) != 0 || !written ||
      std::rename(temporary.c_str(), path.c_str()) != 0) {
    (void)unlink(temporary.c_str());
    return false;
  }
  return true;
}

std::string title_dir(const std::string &root, std::string_view title) {
  return root + "/" + std::string(title);
}

bool make_title_dir(const std::string &root, std::string_view title) {
  (void)mkdir(root.c_str(), 0777);
  const std::string dir = title_dir(root, title);
  return mkdir(dir.c_str(), 0777) == 0 || access(dir.c_str(), W_OK) == 0;
}

void scan(const std::string &dir, std::string_view name_prefix,
          const Config &config, std::vector<Plugin> &out) {
  DIR *handle = opendir(dir.c_str());
  if (!handle)
    return;
  std::vector<Plugin> found;
  while (dirent *entry = readdir(handle)) {
    Plugin plugin;
    if (found.size() >= kMaxPlugins || !plugin_kind(entry->d_name, &plugin.kind))
      continue;
    plugin.path = dir + "/" + entry->d_name;
    struct stat info;
    if (stat(plugin.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
      continue;
    plugin.name = std::string(name_prefix) + entry->d_name;
    plugin.enabled = std::find(config.disabled.begin(), config.disabled.end(),
                               plugin.name) == config.disabled.end();
    found.push_back(std::move(plugin));
  }
  closedir(handle);
  std::sort(found.begin(), found.end(),
            [](const Plugin &a, const Plugin &b) { return a.name < b.name; });
  out.insert(out.end(), found.begin(), found.end());
}

} // namespace

const char *kind_name(Kind kind) noexcept {
  switch (kind) {
  case Kind::Sprx: return "SPRX";
  case Kind::Prx: return "PRX";
  case Kind::Elf: return "ELF";
  }
  return "?";
}

bool plugin_kind(std::string_view file_name, Kind *out) {
  if (file_name.empty() || file_name.front() == '.' ||
      file_name.find_first_of("/=,") != std::string_view::npos)
    return false;
  for (const char c : file_name)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
      return false;
  Kind kind;
  if (ends_with_ci(file_name, ".sprx"))
    kind = Kind::Sprx;
  else if (ends_with_ci(file_name, ".prx"))
    kind = Kind::Prx;
  else if (ends_with_ci(file_name, ".elf"))
    kind = Kind::Elf;
  else
    return false;
  if (out)
    *out = kind;
  return true;
}

bool valid_title(std::string_view title) noexcept {
  return title.size() == 9 &&
         std::all_of(title.begin(), title.end(), [](unsigned char c) {
           return std::isalnum(c) != 0;
         });
}

Config parse_config(std::string_view text) {
  Config config;
  for_each_line(text, [&](std::string_view key, std::string_view value) {
    if (key == "load_on_startup") {
      config.load_on_startup = !(value == "false" || value == "0" || value == "off");
    } else if (key == "disabled") {
      while (!value.empty()) {
        const size_t comma = value.find(',');
        const std::string_view name = trim(value.substr(0, comma));
        if (!name.empty())
          config.disabled.emplace_back(name);
        value = comma == std::string_view::npos ? std::string_view{}
                                                : value.substr(comma + 1);
      }
    }
  });
  return config;
}

std::string serialize_config(const Config &config) {
  std::string text = std::string("load_on_startup=") +
                     (config.load_on_startup ? "true" : "false") + "\n";
  text += "disabled=";
  for (size_t i = 0; i < config.disabled.size(); ++i)
    text += (i ? "," : "") + config.disabled[i];
  return text + "\n";
}

Config load_config(const std::string &root, std::string_view title) {
  if (!valid_title(title))
    return {};
  return parse_config(read_small(title_dir(root, title) + "/config.ini"));
}

bool save_config(const std::string &root, std::string_view title,
                 const Config &config) {
  return valid_title(title) && make_title_dir(root, title) &&
         write_atomic(title_dir(root, title) + "/config.ini",
                      serialize_config(config));
}

std::vector<Plugin> list(const std::string &root, std::string_view title,
                         const Config &config) {
  std::vector<Plugin> plugins;
  if (!valid_title(title))
    return plugins;
  scan(title_dir(root, title), "", config, plugins);
  scan(root + "/" + std::string(kAllFolder), "all/", config, plugins);
  return plugins;
}

Status load_status(const std::string &root, std::string_view title) {
  Status status;
  if (!valid_title(title))
    return status;
  for_each_line(read_small(title_dir(root, title) + "/status.ini"),
                [&](std::string_view key, std::string_view value) {
                  status[std::string(key)] = std::string(value);
                });
  return status;
}

bool save_status(const std::string &root, std::string_view title,
                 const Status &status) {
  if (!valid_title(title) || !make_title_dir(root, title))
    return false;
  std::string text;
  for (const auto &[name, value] : status)
    text += name + "=" + value + "\n";
  return write_atomic(title_dir(root, title) + "/status.ini", text);
}

} // namespace onion::pluginloader
