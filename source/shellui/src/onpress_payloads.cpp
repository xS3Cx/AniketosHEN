/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress payloads / auto-start */
#include "onpress.hpp"
#include "payload_center.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_payloads.hpp"
#include <cerrno>
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <unistd.h>

void *load_payload_thread(void *args);

namespace {

constexpr const char *kRunPrefix = "id_payload_run_";
constexpr const char *kEnabledPrefix = "id_payload_enabled_";
constexpr const char *kDelayPrefix = "id_payload_delay_";

PayloadEntry *payload_for_control(const std::string &control_id,
                                  const char *prefix) {
  const std::string prefix_string(prefix);
  if (control_id.rfind(prefix_string, 0) != 0)
    return nullptr;
  const std::string id = control_id.substr(prefix_string.size());
  for (auto &entry : g_ui.payloads_list)
    if (entry.id == id)
      return &entry;
  return nullptr;
}

bool control_enabled(const std::string &value) {
  return value == "1" || value == "true";
}

} // namespace

void shellui_payload_start(const PayloadEntry &entry) {
  pthread_t thread;
  auto *info = new PayloadEntry(entry);
  if (pthread_create(&thread, nullptr, load_payload_thread, info) == 0) {
    pthread_detach(thread);
    return;
  }
  delete info;
  notify("notify.payload.launch_failed", entry.path.c_str(), entry.tid.c_str());
}

bool shellui_payload_stop(const PayloadEntry &entry) {
  char pid_path[256];
  const int pid = static_cast<int>(shellui_payload_resolve_recorded_pid(
      entry.tid.c_str(), pid_path, sizeof(pid_path)));
  if (pid <= 1)
    return false;
  LOG_DEBUG("killing recorded payload pid: %d (%s)", pid, entry.tid.c_str());
  IPC_Client::getInstance(false).ForceKillPID(pid);
  unlink(pid_path);
  notify("notify.process.killed", entry.name.c_str());
  return true;
}

static OnPressResult prefix_id_payload_run(OnPressContext &ctx) {
  if (ctx.id.rfind(kRunPrefix, 0) != 0) {
    return OnPressResult::NotMine;
  }
  PayloadEntry *entry = payload_for_control(ctx.id, kRunPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  const bool running = shellui_payload_is_running(entry->tid.c_str());
  if (!control_enabled(ctx.value) && running) {
    shellui_payload_stop(*entry);
  } else if (control_enabled(ctx.value) && !running) {
    LOG_DEBUG("Payload %s not running", entry->tid.c_str());
    shellui_payload_start(*entry);
  }
  return OnPressResult::Consumed;
}

bool shellui_payload_set_autostart(PayloadEntry &entry, bool enabled) {
  const std::string marker = entry.shellui_path + ".auto_start";
  if (!enabled) {
    /* Disabling only removes the boot marker; a running payload keeps running. */
    unlink(marker.c_str());
    return true;
  }

  /* Rebuild the whole enabled order so the new payload is last and the
   * priorities stay unique: 1000, 990, 980, ... */
  std::vector<PayloadEntry *> ordered;
  for (auto &other : g_ui.payloads_list) {
    if (other.id == entry.id)
      continue;
    if (access((other.shellui_path + ".auto_start").c_str(), F_OK) != 0)
      continue;
    ordered.push_back(&other);
  }
  ordered.push_back(&entry);

  bool saved = true;
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    OnionPayloadConfig config;
    onion_payload_config_load(ordered[i]->shellui_path.c_str(), &config);
    config.priority = std::max(0, 1000 - static_cast<int>(i) * 10);
    if (!onion_payload_config_save(ordered[i]->shellui_path.c_str(), &config)) {
      notify("notify.payload.config_save_failed", ordered[i]->name.c_str());
      saved = false;
    }
    ordered[i]->config = config;
  }

  const int fd = open(marker.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (fd < 0) {
    notify("notify.payload.autostart_file");
    return false;
  }
  close(fd);
  return saved;
}

static OnPressResult prefix_id_enabled(OnPressContext &ctx) {
  if (ctx.id.rfind(kEnabledPrefix, 0) != 0)
    return OnPressResult::NotMine;
  PayloadEntry *entry = payload_for_control(ctx.id, kEnabledPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  (void)shellui_payload_set_autostart(*entry, control_enabled(ctx.value));
  return OnPressResult::Consumed;
}

static OnPressResult set_payload_delay(OnPressContext &ctx) {
  ctx.dirty = false;
  PayloadEntry *entry = payload_for_control(ctx.id, kDelayPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  OnionPayloadConfig config;
  onion_payload_config_load(entry->shellui_path.c_str(), &config);
  char *end = nullptr;
  errno = 0;
  const long value = std::strtol(ctx.value.c_str(), &end, 10);
  if (ctx.value.empty() || ctx.value.find_first_not_of("0123456789") !=
                               std::string::npos ||
      errno == ERANGE || !end || *end || value < 0 ||
      value > ONION_PAYLOAD_DELAY_MAX) {
    notify("notify.payload.config_invalid", ONION_PAYLOAD_DELAY_MAX);
  } else {
    config.delay_seconds = static_cast<int>(value);
    if (!onion_payload_config_save(entry->shellui_path.c_str(), &config))
      notify("notify.payload.config_save_failed", entry->name.c_str());
  }

  // Rebind the persisted value, including after rejected input or failed writes.
  onion_payload_config_load(entry->shellui_path.c_str(), &entry->config);
  const std::string saved = std::to_string(entry->config.delay_seconds);
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : Root_Domain;
  if (!domain)
    domain = Root_Domain;
  if (ctx.element && domain && set_value_method && mono_string_new && mono_runtime_invoke) {
    MonoString *text = mono_string_new(domain, saved.c_str());
    if (text) {
      void *args[] = {text};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(set_value_method, ctx.element, args, &exception);
      if (exception)
        LOG_ERROR("Failed to restore payload config control value");
    }
  }
  return OnPressResult::Consumed;
}

static OnPressResult update_payload(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_payload_update_";
  const std::string id = ctx.id.substr(std::string(kPrefix).size());
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id == id) {
      if (!onion::shellui::payload_center::start_update_for_path(
              entry.shellui_path))
        notify("notify.payload_center.not_installable");
      return OnPressResult::Consumed;
    }
  }
  return OnPressResult::Consumed;
}

static OnPressResult remove_payload(OnPressContext &ctx) {
  ctx.dirty = false;
  constexpr const char *kPrefix = "id_payload_remove_";
  const std::string id = ctx.id.substr(std::string(kPrefix).size());
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id == id) {
      if (!onion::shellui::payload_center::remove_installed(entry.id,
                                                            entry.name))
        notify("notify.payload_center.remove_failed");
      return OnPressResult::Consumed;
    }
  }
  return OnPressResult::Consumed;
}

static const OnPressPrefixEntry kPrefix[] = {
    {"id_payload_update_", update_payload},
    {"id_payload_remove_", remove_payload},
    {kDelayPrefix, set_payload_delay},
    {kEnabledPrefix, prefix_id_enabled},
    {kRunPrefix, prefix_id_payload_run},
};

const OnPressPrefixEntry *onpress_payloads_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
