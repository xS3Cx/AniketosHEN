#include "payload_progress.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "payload_center.hpp"
#include "onion_cjson.hpp"
#include "progress_auto_return.hpp"
#include "progress_ui3.hpp"
#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"

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

constexpr std::string_view kCustomElementId = "id_pldc_progress_custom";
constexpr int kPollIntervalMs = 500;
constexpr int kRecvTimeoutMs = 800;
constexpr int kMaxFailures = 10;

enum class State { Idle, Running, Ok, Error };

struct ProgressState {
  std::mutex mu;
  State state = State::Idle;
  int progress = 0;
  int displayed_progress = 0;
  int completed = 0;
  int total = 0;
  int indeterminate = 0;
  std::string phase;
  std::string error;
  std::string item;
  std::string identity;
  uint32_t task_id = 0;
  bool task_may_be_running = false;
  progress_ui3::Panel panel;
  progress_ui3::BoundWidget page;
  std::string applied_title;
  std::string applied_phase;
  std::string applied_percent;
  bool percent_applied = false;
  bool cancel_cleared = false;
  int applied_progress = -1;
  int applied_status = -1;
};

ProgressState g_progress;
std::atomic<uint64_t> g_generation{0};

toolbox::ProgressAutoReturn g_auto_return({toolbox::Page::PayloadProgress, 800,
                                           2000});

struct PollArgs {
  uint64_t generation = 0;
  uint32_t task_id = 0;
};

struct CancelArgs {
  uint32_t task_id = 0;
};

bool current(uint64_t generation) {
  return g_generation.load(std::memory_order_acquire) == generation;
}

void release_panel_locked() {
  g_progress.panel.release();
  g_progress.applied_title.clear();
  g_progress.applied_phase.clear();
  g_progress.applied_percent.clear();
  g_progress.percent_applied = false;
  g_progress.cancel_cleared = false;
  g_progress.applied_progress = -1;
  g_progress.applied_status = -1;
}

void reset_locked() {
  release_panel_locked();
  g_progress.state = State::Idle;
  g_progress.progress = 0;
  g_progress.displayed_progress = 0;
  g_progress.completed = 0;
  g_progress.total = 0;
  g_progress.indeterminate = 0;
  g_progress.phase.clear();
  g_progress.error.clear();
  g_progress.item.clear();
  g_progress.identity.clear();
  g_progress.task_id = 0;
  g_progress.task_may_be_running = false;
  progress_ui3::release_widget(g_progress.page);
  g_auto_return.reset();
}

void advance_locked() {
  if (g_progress.state != State::Running)
    return;
  const int target = std::clamp(g_progress.progress, 0, 100);
  if (g_progress.displayed_progress > target)
    g_progress.displayed_progress = target;
  else if (g_progress.displayed_progress < target)
    ++g_progress.displayed_progress;
}

struct Presentation {
  std::string title;
  std::string phase;
  std::string percent;
  int bar = 0;
  int status = 0;
};

std::string error_text(const std::string &error) {
  const char *key = "payload_center.error.unknown";
  if (error == "network")
    key = "payload_center.error.network";
  else if (error == "protocol")
    key = "payload_center.error.protocol";
  else if (error == "tls")
    key = "payload_center.error.tls";
  else if (error == "clock")
    key = "payload_center.error.clock";
  else if (error == "hash")
    key = "payload_center.error.hash";
  else if (error == "elf")
    key = "payload_center.error.elf";
  else if (error == "io")
    key = "payload_center.error.io";
  else if (error == "space")
    key = "payload_center.error.space";
  else if (error == "busy")
    key = "payload_center.error.busy";
  else if (error == "rejected")
    key = "payload_center.error.rejected";
  else if (error == "cancelled")
    key = "payload_center.error.cancelled";
  else if (error == "not_found")
    key = "payload_center.error.not_found";
  return toolbox_i18n::tr(key);
}

Presentation presentation_locked() {
  Presentation view;
  view.title = g_progress.item;
  switch (g_progress.state) {
  case State::Running:
    if (g_progress.phase == "download")
      view.phase = toolbox_i18n::tr("payload_center.phase.download");
    else if (g_progress.phase == "verify")
      view.phase = toolbox_i18n::tr("payload_center.phase.verify");
    else if (g_progress.phase == "install")
      view.phase = toolbox_i18n::tr("payload_center.phase.install");
    else if (g_progress.phase == "cleanup")
      view.phase = toolbox_i18n::tr("payload_center.phase.cleanup");
    else
      view.phase = toolbox_i18n::tr("payload_center.phase.start");
    if (g_progress.progress >= 0) {
      view.bar = g_progress.displayed_progress;
      view.percent = std::to_string(view.bar) + '%';
    } else if (g_progress.phase == "download") {
      view.bar = g_progress.indeterminate;
      view.percent = std::to_string(std::max(g_progress.completed, 0)) + " B";
    }
    return view;
  case State::Ok:
    view.phase = toolbox_i18n::tr("payload_center.progress.ok");
    view.percent = "100%";
    view.bar = 100;
    return view;
  case State::Error:
    view.phase = error_text(g_progress.error);
    view.bar = std::clamp(g_progress.progress, 0, 100);
    view.status = 2;
    return view;
  case State::Idle:
  default:
    view.phase = toolbox_i18n::tr("payload_center.progress.idle");
    return view;
  }
}

void apply_locked() {
  if (!g_progress.panel.valid())
    return;
  advance_locked();
  const Presentation view = presentation_locked();
  if (g_progress.panel.title_label.object &&
      g_progress.applied_title != view.title) {
    if (progress_ui3::set_label_text(g_progress.panel.title_label.object,
                                     view.title))
      g_progress.applied_title = view.title;
  }
  if (g_progress.panel.phase_label.object &&
      g_progress.applied_phase != view.phase) {
    if (progress_ui3::set_label_text(g_progress.panel.phase_label.object,
                                     view.phase))
      g_progress.applied_phase = view.phase;
  }
  if (g_progress.panel.percent_label.object &&
      (!g_progress.percent_applied ||
       g_progress.applied_percent != view.percent)) {
    if (progress_ui3::set_label_text(g_progress.panel.percent_label.object,
                                     view.percent)) {
      g_progress.applied_percent = view.percent;
      g_progress.percent_applied = true;
    }
  }
  if (g_progress.panel.cancel_label.object && !g_progress.cancel_cleared) {
    if (progress_ui3::set_label_text(g_progress.panel.cancel_label.object, ""))
      g_progress.cancel_cleared = true;
  }
  const float normalized = static_cast<float>(view.bar) / 100.0f;
  float actual = 0.0f;
  const bool readback = progress_ui3::read_progress_bar_progress(
      g_progress.panel.progress_bar.object, actual);
  const bool differs =
      !readback || !std::isfinite(actual) ||
      std::abs(actual - normalized) > 0.001f;
  const bool write = readback ? differs
                              : g_progress.applied_progress != view.bar;
  if (write && progress_ui3::set_progress_bar_progress(
                   g_progress.panel.progress_bar.object, normalized))
    g_progress.applied_progress = view.bar;
  else if (readback && g_progress.applied_progress != view.bar)
    g_progress.applied_progress = view.bar;
  if (g_progress.applied_status != view.status) {
    if (progress_ui3::set_progress_bar_status(
            g_progress.panel.progress_bar.object, view.status))
      g_progress.applied_status = view.status;
  }
}

void *cancel_worker(void *raw) {
  std::unique_ptr<CancelArgs> args(static_cast<CancelArgs *>(raw));
  const uint32_t task_id = args ? args->task_id : 0;
  std::string reply;
  if (!IPC_Client::getInstance(true).CancelPayloadInstall(task_id, reply))
    LOG_ERROR("payload_progress: cancel failed task_id=%u", task_id);
  return nullptr;
}

void request_cancel(uint32_t task_id) {
  if (task_id == 0)
    return;
  auto *args = new CancelArgs{task_id};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, cancel_worker, args) != 0) {
    delete args;
    return;
  }
  pthread_detach(thread);
}

void *poll_worker(void *raw) {
  std::unique_ptr<PollArgs> args(static_cast<PollArgs *>(raw));
  const uint64_t generation = args ? args->generation : 0;
  const uint32_t task_id = args ? args->task_id : 0;
  IPC_Client &ipc = IPC_Client::getInstance(true);
  int failures = 0;
  bool terminal = false;
  while (!terminal) {
    usleep(kPollIntervalMs * 1000);
    if (!current(generation))
      return nullptr;
    ipc.set_recv_timeout_ms(kRecvTimeoutMs);
    std::string json;
    if (!ipc.PayloadInstallStatus(json)) {
      if (++failures >= kMaxFailures) {
        std::lock_guard<std::mutex> lock(g_progress.mu);
        if (current(generation)) {
          g_progress.state = State::Error;
          g_progress.error = "io";
        }
        return nullptr;
      }
      continue;
    }
    if (!current(generation))
      return nullptr;
    onion_cjson::Root root(json);
    if (!root) {
      if (++failures >= kMaxFailures)
        return nullptr;
      continue;
    }
    const char *state = onion_cjson::string_item(root.get(), "state", "idle");
    const int reported_task =
        onion_cjson::int_item(root.get(), "task_id", 0);
    if (reported_task >= 0 &&
        static_cast<uint32_t>(reported_task) != task_id) {
      if (++failures >= kMaxFailures)
        return nullptr;
      continue;
    }
    failures = 0;
    const char *phase = onion_cjson::string_item(root.get(), "phase", "");
    const char *error = onion_cjson::string_item(root.get(), "error", "");
    const char *item = onion_cjson::string_item(root.get(), "item", "");
    const char *identity =
        onion_cjson::string_item(root.get(), "identity", "");
    const int progress = onion_cjson::int_item(root.get(), "progress", -1);
    const int completed = onion_cjson::int_item(root.get(), "completed", 0);
    const int total = onion_cjson::int_item(root.get(), "total", 0);
    {
      std::lock_guard<std::mutex> lock(g_progress.mu);
      if (!current(generation))
        return nullptr;
      const std::string next_phase = phase ? phase : "";
      if (g_progress.phase != next_phase) {
        g_progress.displayed_progress = 0;
        g_progress.indeterminate = 0;
      }
      g_progress.phase = next_phase;
      g_progress.error = error ? error : "";
      if (item && item[0])
        g_progress.item = item;
      if (identity && identity[0])
        g_progress.identity = identity;
      g_progress.progress = progress < 0 ? -1 : std::clamp(progress, 0, 100);
      g_progress.completed = std::max(completed, 0);
      g_progress.total = std::max(total, 0);
      if (state && std::strcmp(state, "running") == 0) {
        g_progress.state = State::Running;
        g_progress.task_may_be_running = true;
        if (g_progress.phase == "download" && g_progress.progress < 0)
          g_progress.indeterminate = (g_progress.indeterminate + 1) % 101;
      } else if (state && std::strcmp(state, "ok") == 0) {
        g_progress.state = State::Ok;
        g_progress.progress = 100;
        g_progress.task_may_be_running = false;
        if (!g_progress.identity.empty())
          onion::shellui::payload_center::request_post_install(
              g_progress.identity, g_progress.item);
        terminal = true;
      } else if (state && std::strcmp(state, "error") == 0) {
        g_progress.state = State::Error;
        g_progress.task_may_be_running = false;
        terminal = true;
      } else {
        g_progress.state = State::Idle;
        g_progress.task_may_be_running = false;
        terminal = true;
      }
    }
  }
  return nullptr;
}

}

bool payload_progress_open_page(void) {
  return toolbox_push_resource("pldc_progress.xml");
}

void payload_progress_show(uint32_t task_id) {
  if (!shellui_hooks_are_ready()) {
    LOG_ERROR("payload_progress: hooks not ready");
    return;
  }
  const uint64_t generation =
      g_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    reset_locked();
    g_progress.state = State::Running;
    g_progress.task_id = task_id;
    g_progress.task_may_be_running = task_id != 0;
    g_progress.phase = "start";
    g_progress.progress = 0;
  }
  auto *args = new PollArgs{generation, task_id};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, poll_worker, args) != 0) {
    delete args;
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (current(generation)) {
      g_progress.state = State::Error;
      g_progress.error = "io";
    }
    return;
  }
  pthread_detach(thread);
}

void generate_payload_progress_xml(std::string &xml_buffer) {
  ps5ui::Page page("id_pldc_progress",
                   toolbox_i18n::tr("payload_center.progress.title"));
  page.root_list_size("1340,740")
      .root_restorable(false)
      .user_custom(std::string(kCustomElementId));
  xml_buffer = page.build();
}

void payload_progress_attach_panel(std::string_view id, MonoObject *widget) {
  if (id != kCustomElementId || !widget)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  if (g_progress.panel.attach(widget))
    apply_locked();
}

void payload_progress_bind_page(MonoObject *page) {
  if (!page)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  if (g_progress.page.object == page)
    return;
  progress_ui3::release_widget(g_progress.page);
  (void)progress_ui3::pin_widget(g_progress.page, page, "page");
}

bool payload_progress_handle_popping(MonoObject *outgoing) {
  uint32_t cancel_task = 0;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!outgoing || outgoing != g_progress.page.object)
      return false;
    if (g_progress.task_may_be_running)
      cancel_task = g_progress.task_id;
    g_generation.fetch_add(1, std::memory_order_acq_rel);
    reset_locked();
  }
  request_cancel(cancel_task);
  return true;
}

void shellui_poll_payload_progress(void) {
  toolbox::ProgressOutcome outcome = toolbox::ProgressOutcome::Ongoing;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    apply_locked();
    if (g_progress.state == State::Ok)
      outcome = toolbox::ProgressOutcome::Success;
    else if (g_progress.state == State::Error)
      outcome = toolbox::ProgressOutcome::Failed;
  }
  g_auto_return.poll(outcome);
}
