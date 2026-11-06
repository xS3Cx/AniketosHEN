/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Firmware 11.6 progress pages use a Legacy Settings <user_custom> placeholder.
 * Its real PUI.UI3 Panel is appended to the ListPanelItem created for that
 * placeholder; live updates are then applied directly to the Panel's widgets.
 */

#include "progress_dialog.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "onion_cjson.hpp"
#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"
#include "progress_auto_return.hpp"
#include "progress_ui3.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <unistd.h>

namespace {

using progress_ui3::append_panel;
using progress_ui3::BoundWidget;
using progress_ui3::new_update_progress_panel;
using progress_ui3::pin_widget;
using progress_ui3::read_progress_bar_progress;
using progress_ui3::release_widget;
using progress_ui3::set_label_text;
using progress_ui3::set_progress_bar_progress;
using progress_ui3::set_progress_bar_status;
using progress_ui3::widget_child_at;
using progress_ui3::widget_children_count;

toolbox::ProgressAutoReturn g_cheat_auto_return({
    toolbox::Page::CheatProgress,
    800,  // success dwell ms
    2000  // error dwell ms
});

constexpr int kPollIntervalMs = 500;
constexpr int kRecvTimeoutMs = 800;
constexpr int kMaxConsecutiveStatusFailures = 10;

constexpr std::string_view kCustomElementId = "id_cheat_progress_custom";

constexpr const char *kPuiUi3Namespace = "Sce.PlayStation.PUI.UI3";
constexpr const char *kUpdatePanelNamespace =
    "Sce.Vsh.ShellUI.Settings.Peripherals.MorpheusUpdateUI3";
constexpr const char *kUpdatePanelClass = "UpdateProgressPanel";

enum class SyncState { Idle, Running, Ok, Error };

enum class SyncPhase { Start, Download, Extract, Install, Cleanup, Unknown };

struct CheatProgress {
  std::mutex mu;
  SyncState state = SyncState::Idle;
  int progress = 0;
  int displayed_progress = 0;
  int completed = 0;
  int total = 0;
  int indeterminate_progress = 0;
  std::string phase;
  std::string error;
  uint32_t task_id = 0;
  bool task_may_be_running = false;
  BoundWidget page;
  BoundWidget host;
  BoundWidget panel;
  BoundWidget title_label;
  BoundWidget phase_label;
  BoundWidget percent_label;
  BoundWidget cancel_label;
  BoundWidget progress_bar;
  bool title_cleared = false;
  std::string applied_phase;
  std::string applied_percent;
  bool percent_applied = false;
  bool cancel_cleared = false;
  int applied_progress = -1;
  int applied_bar_status = -1;
};

CheatProgress g_progress;
std::atomic<uint64_t> g_session_generation{0};

struct PollArgs {
  uint64_t generation = 0;
  uint32_t task_id = 0;
};

struct CancelArgs {
  uint32_t task_id = 0;
};

void release_bound_widgets_locked() {
  release_widget(g_progress.host);
  release_widget(g_progress.panel);
  release_widget(g_progress.title_label);
  release_widget(g_progress.phase_label);
  release_widget(g_progress.percent_label);
  release_widget(g_progress.cancel_label);
  release_widget(g_progress.progress_bar);
  g_progress.title_cleared = false;
  g_progress.applied_phase.clear();
  g_progress.applied_percent.clear();
  g_progress.percent_applied = false;
  g_progress.cancel_cleared = false;
  g_progress.applied_progress = -1;
  g_progress.applied_bar_status = -1;
}

void reset_progress_locked() {
  release_bound_widgets_locked();
  g_progress.state = SyncState::Idle;
  g_progress.progress = 0;
  g_progress.displayed_progress = 0;
  g_progress.completed = 0;
  g_progress.total = 0;
  g_progress.indeterminate_progress = 0;
  g_progress.phase.clear();
  g_progress.error.clear();
  g_progress.task_id = 0;
  release_widget(g_progress.page);
  g_progress.task_may_be_running = false;
  g_cheat_auto_return.reset();
}

bool session_is_current(uint64_t generation) {
  return g_session_generation.load(std::memory_order_acquire) == generation;
}

void *cancel_cheat_sync(void *raw) {
  std::unique_ptr<CancelArgs> args(static_cast<CancelArgs *>(raw));
  const uint32_t task_id = args ? args->task_id : 0;
  IPC_Client &ipc = IPC_Client::getInstance(true);
  std::string reply;
  if (!ipc.CancelCheatSync(task_id, reply)) {
    LOG_ERROR("cheat_progress_xml: cancel request failed task_id=%u", task_id);
  } else {
    LOG_DEBUG("cheat_progress_xml: cancel task_id=%u reply=%s", task_id,
              reply.c_str());
  }
  return nullptr;
}

void request_cancel_async(uint32_t task_id) {
  auto *args = new CancelArgs{task_id};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, cancel_cheat_sync, args) != 0) {
    delete args;
    LOG_ERROR("cheat_progress_xml: cancel thread create failed task_id=%u",
              task_id);
    return;
  }
  pthread_detach(thread);
}

SyncPhase parse_phase(const std::string &phase) {
  if (phase == "start")
    return SyncPhase::Start;
  if (phase == "download")
    return SyncPhase::Download;
  if (phase == "extract")
    return SyncPhase::Extract;
  if (phase == "install")
    return SyncPhase::Install;
  if (phase == "cleanup")
    return SyncPhase::Cleanup;
  return SyncPhase::Unknown;
}

void advance_displayed_progress_locked() {
  if (g_progress.state != SyncState::Running) {
    return;
  }

  const SyncPhase phase = parse_phase(g_progress.phase);
  if (g_progress.progress < 0 || phase == SyncPhase::Start) {
    g_progress.displayed_progress = 0;
    return;
  }

  const int target = std::clamp(g_progress.progress, 0, 100);
  if (g_progress.displayed_progress > target) {
    g_progress.displayed_progress = target;
  } else if (g_progress.displayed_progress < target) {
    ++g_progress.displayed_progress;
  }
}

struct ProgressPresentation {
  std::string phase;
  std::string percent;
  int bar_progress = 0;
  int bar_status = 0;
};

std::string downloaded_size_text(int bytes) {
  constexpr int kMegabyte = 1024 * 1024;
  const int safe_bytes = std::max(bytes, 0);
  const int whole = safe_bytes / kMegabyte;
  const int tenths = ((safe_bytes % kMegabyte) * 10) / kMegabyte;
  return std::to_string(whole) + '.' + std::to_string(tenths) + " MB";
}

std::string sync_error_text(const std::string &error) {
  const char *key = nullptr;
  if (error.empty() || error == "sync_failed") {
    key = "cheats.sync.error.unknown";
  } else if (error == "status_unavailable") {
    key = "cheats.sync.error.status";
  } else if (error == "no_space") {
    key = "cheats.sync.error.no_space";
  } else if (error == "archive download failed") {
    key = "cheats.sync.error.network";
  } else if (error == "tls_verify") {
    key = "cheats.sync.error.tls";
  } else if (error == "system_clock") {
    key = "cheats.sync.error.clock";
  } else if (error == "temp directory failed") {
    key = "cheats.sync.error.storage";
  } else if (error == "archive extract failed") {
    key = "cheats.sync.error.extract";
  } else if (error == "install failed") {
    key = "cheats.sync.error.install";
  } else if (error == "unknown_catalog" ||
             error == "refusing non-https archive" ||
             error == "sync input rejected") {
    key = "cheats.sync.error.invalid";
  } else if (error == "thread" || error == "poll_thread") {
    key = "cheats.sync.error.service";
  }
  return key ? toolbox_i18n::tr(key)
             : toolbox_i18n::format("cheats.sync.error.detail_fmt",
                                    error.c_str());
}

ProgressPresentation presentation_locked() {
  ProgressPresentation view;
  switch (g_progress.state) {
  case SyncState::Running: {
    const SyncPhase phase = parse_phase(g_progress.phase);
    switch (phase) {
    case SyncPhase::Start:
      view.phase = toolbox_i18n::tr("cheats.sync.phase.preparing");
      break;
    case SyncPhase::Download:
      view.phase = toolbox_i18n::tr("cheats.sync.dialog.message");
      break;
    case SyncPhase::Extract:
      view.phase = toolbox_i18n::tr("cheats.sync.phase.extracting");
      break;
    case SyncPhase::Install:
      view.phase = toolbox_i18n::tr("cheats.sync.phase.installing");
      break;
    case SyncPhase::Cleanup:
      view.phase = toolbox_i18n::tr("cheats.sync.phase.cleaning");
      break;
    case SyncPhase::Unknown:
      view.phase = toolbox_i18n::tr("cheats.sync.running");
      break;
    }

    // A negative value means this phase has not learned its total yet. Keep it
    // indeterminate instead of displaying or retaining a misleading percent.
    if (g_progress.progress >= 0 && phase != SyncPhase::Start) {
      view.bar_progress = g_progress.displayed_progress;
      view.percent = std::to_string(view.bar_progress) + '%';
    } else if (phase == SyncPhase::Download) {
      view.bar_progress = g_progress.indeterminate_progress;
      view.percent = downloaded_size_text(g_progress.completed);
    }
    return view;
  }
  case SyncState::Ok:
    view.phase = toolbox_i18n::tr("cheats.sync.ok");
    view.percent = "100%";
    view.bar_progress = 100;
    return view;
  case SyncState::Error:
    view.phase = sync_error_text(g_progress.error);
    view.bar_progress = std::clamp(g_progress.progress, 0, 100);
    view.bar_status = 2;
    return view;
  case SyncState::Idle:
  default:
    view.phase = toolbox_i18n::tr("cheats.sync.idle");
    return view;
  }
}

void apply_progress_locked() {
  if (!g_progress.progress_bar.object)
    return;

  advance_displayed_progress_locked();
  const ProgressPresentation view = presentation_locked();

  if (g_progress.title_label.object && !g_progress.title_cleared) {
    if (set_label_text(g_progress.title_label.object, ""))
      g_progress.title_cleared = true;
  }
  if (g_progress.phase_label.object &&
      g_progress.applied_phase != view.phase) {
    if (set_label_text(g_progress.phase_label.object, view.phase))
      g_progress.applied_phase = view.phase;
  }
  if (g_progress.percent_label.object &&
      (!g_progress.percent_applied ||
       g_progress.applied_percent != view.percent)) {
    if (set_label_text(g_progress.percent_label.object, view.percent)) {
      g_progress.applied_percent = view.percent;
      g_progress.percent_applied = true;
    }
  }
  if (g_progress.cancel_label.object && !g_progress.cancel_cleared) {
    if (set_label_text(g_progress.cancel_label.object, ""))
      g_progress.cancel_cleared = true;
  }
  const float normalized = static_cast<float>(view.bar_progress) / 100.0f;
  float actual = 0.0f;
  const bool readback =
      read_progress_bar_progress(g_progress.progress_bar.object, actual);
  const bool differs =
      !readback || !std::isfinite(actual) ||
      std::abs(actual - normalized) > 0.001f;
  const bool needs_write =
      readback ? differs : (g_progress.applied_progress != view.bar_progress);
  if (needs_write &&
      set_progress_bar_progress(g_progress.progress_bar.object, normalized)) {
    g_progress.applied_progress = view.bar_progress;
    float after = 0.0f;
    const bool after_readback =
        read_progress_bar_progress(g_progress.progress_bar.object, after);
    LOG_DEBUG(
        "cheat_progress_ui3: Progress write target=%.3f before=%s%.3f "
        "after=%s%.3f",
        normalized, readback ? "" : "?", readback ? actual : 0.0f,
        after_readback ? "" : "?", after_readback ? after : 0.0f);
  } else if (readback && g_progress.applied_progress != view.bar_progress) {
    g_progress.applied_progress = view.bar_progress;
  }
  if (g_progress.applied_bar_status != view.bar_status) {
    if (set_progress_bar_status(g_progress.progress_bar.object,
                                view.bar_status))
      g_progress.applied_bar_status = view.bar_status;
  }
}

bool build_and_append_panel_locked(MonoObject *widget) {
  if (g_progress.host.object == widget && g_progress.panel.object) {
    apply_progress_locked();
    return true;
  }

  release_bound_widgets_locked();

  MonoObject *panel = new_update_progress_panel();
  if (!panel) {
    LOG_ERROR("cheat_progress_ui3: official progress panel unavailable");
    return false;
  }

  if (!pin_widget(g_progress.host, widget, "host widget") ||
      !pin_widget(g_progress.panel, panel, "panel")) {
    release_bound_widgets_locked();
    return false;
  }

  if (!append_panel(widget, panel)) {
    LOG_ERROR("cheat_progress_ui3: Widget.AppendChild(Widget) failed");
    release_bound_widgets_locked();
    return false;
  }

  const int children_count = widget_children_count(panel);
  LOG_DEBUG("cheat_progress_ui3: official panel children=%d", children_count);
  if (children_count < 5) {
    LOG_ERROR("cheat_progress_ui3: official panel has too few children");
    release_bound_widgets_locked();
    return false;
  }

  MonoObject *title = widget_child_at(panel, 0);
  MonoObject *phase = widget_child_at(panel, 1);
  MonoObject *bar = widget_child_at(panel, 2);
  MonoObject *percent = widget_child_at(panel, 3);
  MonoObject *cancel = widget_child_at(panel, 4);

  if (!pin_widget(g_progress.title_label, title, "title") ||
      !pin_widget(g_progress.phase_label, phase, "version") ||
      !pin_widget(g_progress.progress_bar, bar, "bar") ||
      !pin_widget(g_progress.percent_label, percent, "progress") ||
      !pin_widget(g_progress.cancel_label, cancel, "cancel")) {
    release_bound_widgets_locked();
    return false;
  }

  apply_progress_locked();
  LOG_DEBUG("cheat_progress_ui3: official progress panel appended");
  return true;
}

void *cheat_progress_poll(void *raw) {
  std::unique_ptr<PollArgs> args(static_cast<PollArgs *>(raw));
  const uint64_t generation = args ? args->generation : 0;
  const uint32_t task_id = args ? args->task_id : 0;
  IPC_Client &ipc = IPC_Client::getInstance(true);
  bool terminal = false;
  int consecutive_failures = 0;
  const auto status_failed = [&consecutive_failures, generation]() {
    if (!session_is_current(generation))
      return true;
    if (++consecutive_failures < kMaxConsecutiveStatusFailures)
      return false;

    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!session_is_current(generation))
      return true;
    g_progress.state = SyncState::Error;
    g_progress.error = "status_unavailable";
    LOG_ERROR("cheat_progress_xml: status unavailable after %d attempts",
              consecutive_failures);
    return true;
  };

  while (!terminal) {
    usleep(kPollIntervalMs * 1000);
    if (!session_is_current(generation))
      return nullptr;
    ipc.set_recv_timeout_ms(kRecvTimeoutMs);

    std::string json;
    if (!ipc.CheatSyncStatus(json)) {
      if (status_failed())
        return nullptr;
      continue;
    }
    if (!session_is_current(generation))
      return nullptr;

    onion_cjson::Root root(json);
    if (!root.get()) {
      if (status_failed())
        return nullptr;
      continue;
    }
    const char *state = onion_cjson::string_item(root.get(), "state", "idle");
    const int reported_task_id =
        onion_cjson::int_item(root.get(), "task_id", 0);
    const char *phase = onion_cjson::string_item(root.get(), "phase", "");
    const char *error = onion_cjson::string_item(root.get(), "error", "");
    const int progress = onion_cjson::int_item(root.get(), "progress", -1);
    const int completed = onion_cjson::int_item(root.get(), "completed", 0);
    const int total = onion_cjson::int_item(root.get(), "total", 0);
    if (reported_task_id < 0 ||
        static_cast<uint32_t>(reported_task_id) != task_id) {
      LOG_ERROR("cheat_progress_xml: status task mismatch expected=%u got=%d",
                task_id, reported_task_id);
      if (status_failed())
        return nullptr;
      continue;
    }
    consecutive_failures = 0;

    {
      std::lock_guard<std::mutex> lock(g_progress.mu);
      if (!session_is_current(generation))
        return nullptr;
      const std::string next_phase = phase ? phase : "";
      if (g_progress.phase != next_phase) {
        g_progress.displayed_progress = 0;
        g_progress.indeterminate_progress = 0;
      }
      g_progress.phase = next_phase;
      g_progress.error = error ? error : "";
      g_progress.progress =
          progress < 0 ? -1 : std::clamp(progress, 0, 100);
      g_progress.completed = std::max(completed, 0);
      g_progress.total = std::max(total, 0);

      if (state && std::strcmp(state, "running") == 0) {
        g_progress.state = SyncState::Running;
        g_progress.task_may_be_running = true;
        if (parse_phase(g_progress.phase) == SyncPhase::Download &&
            g_progress.progress < 0) {
          // Keep an unknown-length transfer visibly active without labeling
          // the synthetic bar position as a percentage.
          g_progress.indeterminate_progress =
              (g_progress.indeterminate_progress + 1) % 101;
        }
      } else if (state && std::strcmp(state, "ok") == 0) {
        g_progress.state = SyncState::Ok;
        g_progress.progress = 100;
        g_progress.task_may_be_running = false;
        terminal = true;
      } else if (state && std::strcmp(state, "error") == 0) {
        g_progress.state = SyncState::Error;
        g_progress.task_may_be_running = false;
        terminal = true;
      } else {
        g_progress.state = SyncState::Idle;
        g_progress.task_may_be_running = false;
        terminal = true;
      }

      LOG_DEBUG("cheat_progress_xml: task_id=%u state=%s phase=%s progress=%d "
                "completed=%d total=%d error=%s",
                task_id, state ? state : "", phase ? phase : "", progress,
                completed, total, error ? error : "");
    }
  }
  return nullptr;
}

} // namespace

bool cheat_progress_open_page(void) {
  return toolbox_push_resource("cheat_progress.xml");
}

void cheat_progress_show(uint32_t task_id) {
  if (!shellui_hooks_are_ready()) {
    LOG_ERROR("cheat_progress_xml: hooks not ready");
    return;
  }

  const uint64_t generation =
      g_session_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    reset_progress_locked();
    g_progress.state = SyncState::Running;
    g_progress.task_id = task_id;
    g_progress.task_may_be_running = task_id != 0;
    g_progress.progress = 0;
    g_progress.displayed_progress = 0;
    g_progress.completed = 0;
    g_progress.total = 0;
    g_progress.indeterminate_progress = 0;
    g_progress.phase = "start";
    g_progress.error.clear();
  }

  auto *args = new PollArgs{generation, task_id};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, cheat_progress_poll, args) != 0) {
    delete args;
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (session_is_current(generation)) {
      g_progress.state = SyncState::Error;
      g_progress.error = "poll_thread";
    }
    LOG_ERROR("cheat_progress_xml: pthread_create failed");
    return;
  }
  pthread_detach(thread);
}

void generate_cheat_progress_xml(std::string &xml_buffer) {
  ps5ui::Page page("id_cheat_sync_progress",
                   toolbox_i18n::tr("cheats.sync.dialog.title"));
  page.root_list_size("1340,740")
      .root_restorable(false)
      .user_custom(std::string(kCustomElementId));
  xml_buffer = page.build();
  LOG_DEBUG("cheat_progress_xml: generated %zu bytes", xml_buffer.size());
}

void cheat_progress_attach_panel(std::string_view id, MonoObject *widget) {
  if (id != kCustomElementId || !widget)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  (void)build_and_append_panel_locked(widget);
}

void cheat_progress_bind_page(MonoObject *page) {
  if (!page)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  if (g_progress.page.object == page)
    return;
  release_widget(g_progress.page);
  (void)pin_widget(g_progress.page, page, "page");
}

bool cheat_progress_handle_popping(MonoObject *outgoing) {
  uint32_t cancel_task_id = 0;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!outgoing || outgoing != g_progress.page.object)
      return false;

    if (g_progress.task_may_be_running)
      cancel_task_id = g_progress.task_id;
    g_session_generation.fetch_add(1, std::memory_order_acq_rel);
    reset_progress_locked();
  }

  if (cancel_task_id != 0)
    request_cancel_async(cancel_task_id);
  return true;
}

void shellui_poll_cheat_progress(void) {
  toolbox::ProgressOutcome outcome = toolbox::ProgressOutcome::Ongoing;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    apply_progress_locked();

    if (g_progress.state == SyncState::Ok) {
      outcome = toolbox::ProgressOutcome::Success;
    } else if (g_progress.state == SyncState::Error) {
      outcome = toolbox::ProgressOutcome::Failed;
    }
  }

  g_cheat_auto_return.poll(outcome);
}

