#include "sprx_plugin_manager_runtime.hpp"

#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/log.h>
#include <onion/notify.h>
#include <onion/notify_i18n.h>
#include <onion/payload.h>
#include <onion/plugin_folder.hpp>
#include <onion/proc_query.h>
#include <onion/sprx_plugin_manager.hpp>

#include <ps5/kernel.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace onion::daemon::sprx_plugins {
namespace {

class CurrentGameTarget final : public onion::sprx::ISprxTargetProvider {
public:
  bool current(onion::sprx::SprxTarget *out) noexcept override {
    if (!out)
      return false;
    std::string title_id;
    int app_id = -1;
    const int pid = get_game_pid();
    if (pid <= 1 || !Get_Running_App_TID(title_id, app_id))
      return false;
    out->pid = static_cast<pid_t>(pid);
    out->title_id = std::move(title_id);
    return true;
  }
};

class PreparedTargetAccess final : public onion::sprx::ITargetAccessPolicy {
public:
  onion::sprx::AccessResult prepare(pid_t pid,
                                     std::string_view) noexcept override {
    if (pid <= 1 || !onion_proc_is_alive(pid) || kernel_get_proc(pid) == 0)
      return {onion::sprx::AccessStatus::PrepareFailed};
    return kernel_get_ucred_uid(pid) == 0
               ? onion::sprx::AccessResult{onion::sprx::AccessStatus::Allowed}
               : onion::sprx::AccessResult{onion::sprx::AccessStatus::Denied};
  }

  onion::sprx::AccessResult restore(pid_t pid,
                                     std::string_view) noexcept override {
    return pid > 1 && onion_proc_is_alive(pid)
               ? onion::sprx::AccessResult{onion::sprx::AccessStatus::Allowed}
               : onion::sprx::AccessResult{onion::sprx::AccessStatus::RestoreFailed};
  }
};

struct Runtime {
  std::mutex mutex;
  onion::sprx::PtraceSprxRuntime transport;
  PreparedTargetAccess access;
  CurrentGameTarget target;
  onion::sprx::SprxPluginManager manager;
  /* Plugin ID -> why it last failed to load; dropped when it loads. */
  std::map<std::string, std::string> last_error;

  Runtime() : manager(transport, access, target) {}
};

struct FolderFile {
  std::string id;
  std::string path;
};

std::vector<FolderFile> folder_files() {
  std::vector<FolderFile> files;
  DIR *dir = opendir(kFolder);
  if (!dir)
    return files;
  while (dirent *entry = readdir(dir)) {
    const std::string id = onion::sprx::plugin_id_for_file(entry->d_name);
    if (!id.empty())
      files.push_back({id, std::string(kFolder) + "/" + entry->d_name});
  }
  closedir(dir);
  std::sort(files.begin(), files.end(),
            [](const FolderFile &a, const FolderFile &b) { return a.id < b.id; });
  return files;
}

/* A first start has no folder and no catalog: make both, so the folder is
   there to drop files in and the catalog loads (empty). */
void ensure_catalog() {
  (void)mkdir(kFolder, 0777);
  const int fd = open(kCatalogPath, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd >= 0) {
    close(fd);
    LOG_INFO("[sprx-manager] created empty catalog %s", kCatalogPath);
  }
}

Runtime &runtime() {
  static Runtime value;
  return value;
}

std::string failure_text(const onion::sprx::SprxPluginResult &item) {
  char text[96];
  if (item.skipped_dependency)
    std::snprintf(text, sizeof(text), "dependency failed");
  else if (item.load.status == onion::sprx::LoadStatus::RemoteError)
    std::snprintf(text, sizeof(text), "%s 0x%08x",
                  onion::sprx::load_status_name(item.load.status),
                  static_cast<uint32_t>(item.load.remote_result));
  else
    std::snprintf(text, sizeof(text), "%s",
                  onion::sprx::load_status_name(item.load.status));
  return text;
}

void log_report(const char *operation,
                const onion::sprx::SprxStartupReport &report) {
  for (const auto &item : report.results) {
    if (item.load.succeeded()) {
      runtime().last_error.erase(item.id);
      continue;
    }
    const std::string text = failure_text(item);
    runtime().last_error[item.id] = text;
    LOG_WARN("[sprx-manager] %s: %s", item.id.c_str(), text.c_str());
  }
  for (const auto &issue : report.issues)
    LOG_WARN("[sprx-manager] line=%zu plugin=%s: %s", issue.line,
             issue.plugin_id.empty() ? "-" : issue.plugin_id.c_str(),
             issue.message.c_str());
  size_t loaded = 0;
  size_t failed = 0;
  size_t skipped = 0;
  for (const auto &item : report.results) {
    if (item.skipped_dependency)
      ++skipped;
    else if (item.load.succeeded())
      ++loaded;
    else
      ++failed;
    if (item.load.plugin_load_called && item.load.plugin_load_result != 0)
      LOG_WARN("[sprx-manager] plugin %s plugin_load returned %d (plugin_unload ran)",
               item.id.c_str(), item.load.plugin_load_result);
  }
  LOG_INFO("[sprx-manager] %s catalog=%d entries=%zu loaded=%zu failed=%zu skipped=%zu",
           operation, report.catalog_loaded ? 1 : 0, report.results.size(),
           loaded, failed, skipped);
}

void run(const char *operation, bool reload) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  if (reload) {
    std::vector<onion::sprx::SprxCatalogIssue> issues;
    if (!current.manager.load_catalog(
            kCatalogPath, &issues,
            {kPluginsIniPath, kGoldHenPluginsIniPath})) {
      for (const auto &issue : issues)
        LOG_WARN("[sprx-manager] line=%zu: %s", issue.line, issue.message.c_str());
      return;
    }
  }
  const auto report = reload ? current.manager.start() : current.manager.reconcile();
  log_report(operation, report);
}

bool is_current_target(Runtime &current, pid_t pid, uint32_t app_id,
                       std::string_view title_id) {
  onion::sprx::SprxTarget target;
  if (!current.target.current(&target)) {
    LOG_DEBUG("[sprx-manager] lifecycle target unavailable pid=%d",
              static_cast<int>(pid));
    return false;
  }
  if (target.pid != pid || target.title_id != title_id) {
    LOG_DEBUG("[sprx-manager] stale Big App start pid=%d tid=%.*s current_pid=%d current_tid=%s",
              static_cast<int>(pid), static_cast<int>(title_id.size()),
              title_id.data(), static_cast<int>(target.pid),
              target.title_id.c_str());
    return false;
  }
  (void)app_id;
  return true;
}

/* "PluginLoader" and one line per plugin: name [TYPE] and how it went, so
   the player sees what the folder loaded. */
void notify_loaded(const std::vector<onion::pluginloader::Plugin> &plugins,
                   const onion::pluginloader::Status &status) {
  if (!g_settings.snapshot().pluginloader_notify)
    return;
  constexpr size_t kMaxLines = 8;
  std::string lines;
  size_t shown = 0;
  for (const auto &plugin : plugins) {
    const auto found = status.find(plugin.name);
    if (found == status.end())
      continue;
    if (++shown > kMaxLines) {
      lines += "\n...";
      break;
    }
    const std::string &state = found->second;
    const char *key = state == "loaded"    ? "notify.pluginloader.loaded"
                      : state == "started" ? "notify.pluginloader.started"
                      : state == "running" ? "notify.pluginloader.running"
                                           : "notify.pluginloader.failed";
    char line[256];
    std::snprintf(line, sizeof(line), "%s%s [%s] %s", lines.empty() ? "" : "\n",
                  plugin.name.c_str(), onion::pluginloader::kind_name(plugin.kind),
                  onion_notify_tr(key));
    lines += line;
  }
  if (!lines.empty())
    onion_notify(true, "notify.pluginloader.summary", lines.c_str());
}

/* The title's folder plugins: modules through the SPRX loader (lock held),
   then each ELF as its own process. Leaves the status for the Toolbox. */
void load_folder_plugins(Runtime &current, std::unique_lock<std::mutex> &lock,
                         std::string_view title_id) {
  namespace pl = onion::pluginloader;
  const pl::Config config = pl::load_config(kPluginLoaderRoot, title_id);
  std::vector<pl::Plugin> plugins = pl::list(kPluginLoaderRoot, title_id, config);
  std::erase_if(plugins, [](const pl::Plugin &p) { return !p.enabled; });
  if (plugins.empty())
    return;
  if (!config.load_on_startup) {
    LOG_INFO("[pluginloader] %.*s: load on startup is off",
             static_cast<int>(title_id.size()), title_id.data());
    return;
  }
  pl::Status status;
  std::vector<onion::sprx::SprxManifestEntry> modules;
  for (const pl::Plugin &plugin : plugins) {
    if (plugin.kind == pl::Kind::Elf)
      continue;
    onion::sprx::SprxManifestEntry entry;
    entry.id = plugin.name;
    entry.path = plugin.path;
    modules.push_back(std::move(entry));
  }
  const auto report = current.manager.load(title_id, modules);
  log_report("pluginloader", report);
  for (const auto &item : report.results)
    status[item.id] = item.load.succeeded() ? "loaded" : failure_text(item);
  lock.unlock(); /* an ELF start waits on the loader socket */

  for (const pl::Plugin &plugin : plugins) {
    if (plugin.kind != pl::Kind::Elf)
      continue;
    /* The payload autostart identity: the same file is not started twice
       while it still runs (an all/ ELF across games too). */
    char key[ONION_PAYLOAD_IDENTITY_SIZE];
    if (!onion_payload_identity_from_path(plugin.path.c_str(), key, sizeof(key))) {
      status[plugin.name] = "invalid path";
      continue;
    }
    const std::string file = plugin.path.substr(plugin.path.rfind('/') + 1);
    if (onion_payload_running(key))
      status[plugin.name] = "running";
    else
      status[plugin.name] =
          onion_payload_load_with_key(plugin.path.c_str(), file.c_str(), key)
              ? "started"
              : "start failed";
    LOG_INFO("[pluginloader] %.*s: %s %s", static_cast<int>(title_id.size()),
             title_id.data(), plugin.name.c_str(), status[plugin.name].c_str());
  }
  notify_loaded(plugins, status);
  if (!pl::save_status(kPluginLoaderRoot, title_id, status))
    LOG_WARN("[pluginloader] cannot write the status of %.*s",
             static_cast<int>(title_id.size()), title_id.data());
}

} // namespace

void start() {
  ensure_catalog();
  (void)mkdir(kPluginLoaderRoot, 0777);
  (void)mkdir((std::string(kPluginLoaderRoot) + "/" +
               std::string(onion::pluginloader::kAllFolder)).c_str(), 0777);
  run("startup", true);
}
void reconcile() { run("reconcile", false); }

std::vector<onion::sprx::SprxInventoryEntry> inventory() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  std::vector<onion::sprx::SprxInventoryEntry> entries =
      current.manager.inventory();
  for (auto &entry : entries) {
    const auto error = current.last_error.find(entry.id);
    if (error != current.last_error.end())
      entry.last_error = error->second;
  }
  for (const FolderFile &file : folder_files()) {
    const bool named = std::any_of(entries.begin(), entries.end(),
                                   [&](const auto &entry) {
                                     return entry.id == file.id ||
                                            entry.path == file.path;
                                   });
    if (named)
      continue;
    onion::sprx::SprxInventoryEntry entry;
    entry.id = file.id;
    entry.path = file.path;
    entry.enabled = false;
    entries.push_back(std::move(entry));
  }
  return entries;
}

onion::sprx::SprxOperationResult set_enabled(std::string_view id,
                                             bool enabled) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.set_enabled(id, enabled);
}

onion::sprx::SprxOperationResult remove(std::string_view id) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  std::string path;
  const onion::sprx::SprxCatalog catalog = current.manager.catalog();
  if (const auto *entry = catalog.find(id)) {
    path = entry->path;
    const auto result = current.manager.remove(id);
    if (!result)
      return result;
  } else {
    for (const FolderFile &file : folder_files())
      if (file.id == id) {
        path = file.path; /* the file shown under this id: the first one */
        break;
      }
    if (path.empty())
      return {false, "SPRX plugin is not declared"};
  }
  current.last_error.erase(std::string(id));
  /* Only a file of the plugins folder is the player's to delete from here. */
  const std::string folder = std::string(kFolder) + "/";
  if (path.compare(0, folder.size(), folder) == 0 &&
      path.find('/', folder.size()) == std::string::npos &&
      unlink(path.c_str()) != 0 && errno != ENOENT)
    return {false, "cannot delete " + path};
  LOG_INFO("[sprx-manager] removed %.*s (%s)", static_cast<int>(id.size()),
           id.data(), path.c_str());
  return {true, {}};
}

void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id) {
  Runtime &current = runtime();
  std::unique_lock<std::mutex> lock(current.mutex);
  if (!is_current_target(current, pid, app_id, title_id))
    return;
  log_report("big-app-start", current.manager.reconcile());
  load_folder_plugins(current, lock, title_id);
}

void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id) {
  LOG_INFO("[sprx-manager] target exited pid=%d appid=%u tid=%.*s; no daemon-owned SPRX runtime or UI session to release",
           static_cast<int>(pid), app_id, static_cast<int>(title_id.size()),
           title_id.data());
}

void stop() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  current.manager.stop();
  LOG_INFO("[sprx-manager] stopped target-bound modules");
}

} // namespace onion::daemon::sprx_plugins
