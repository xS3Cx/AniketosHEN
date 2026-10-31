/* Copyright (C) 2026 AniketosHEN — OnPress for the Keyboard and Mouse pages.
 *
 * Every control id is "id_kbm-<path>-<field>" (see kbm_toolbox.hpp). Lists
 * and toggles write one value and save; buttons add, delete or capture.
 * Work that must not run inside a press (key capture, closing or redrawing
 * the page) waits for the next frame in Tick().
 */
#include "onpress.hpp"

#include "aniketos_shell/kbm_capture.hpp"
#include "kbm_toolbox.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_navigation.hpp"

#include <ctime>

namespace kbm_toolbox {
namespace {

constexpr uint32_t kCaptureMs = 8000;
constexpr uint64_t kPopGiveUpMs = 3000;

struct Capture {
  bool active = false;
  Path target;
  MonoObject *page = nullptr;
};
Capture s_capture;
MonoObject *s_refresh_page = nullptr; /* redraw on the next frame */
int s_pops = 0;                       /* pages to close after a delete */
uint64_t s_pop_deadline = 0;
MonoObject *s_deleted_page = nullptr; /* redrawn as "deleted" if it stays */

uint64_t now_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

void capture_begin(const Path &target, MonoObject *page) {
  if (s_capture.active)
    KbmCapture::EndCapture();
  const bool keyboard = KbmCapture::BeginCapture(kCaptureMs);
  s_capture = {true, target, page};
  notify(keyboard ? "notify.kbm.capture" : "notify.kbm.capture_mouse_only");
}

void capture_tick() {
  if (!s_capture.active)
    return;
  const KbmCapture::Result r = KbmCapture::PollCapture();
  if (r.state == KbmCapture::State::None)
    return;
  s_capture.active = false;
  KbmCapture::EndCapture();
  if (r.state == KbmCapture::State::TimedOut) {
    notify("notify.kbm.capture_timeout");
    return;
  }
  if (r.state != KbmCapture::State::Captured)
    return; /* cancelled on purpose: nothing to say */
  const std::string name = input_name(r.input);
  switch (set_input(s_capture.target, r.input, r.modifiers)) {
  case SetInput::Ok:
    notify("notify.kbm.captured", name.c_str());
    s_refresh_page = s_capture.page;
    break;
  case SetInput::Reserved:
    notify("notify.kbm.reserved", name.c_str());
    break;
  case SetInput::Gone:
    notify("notify.kbm.gone");
    break;
  case SetInput::Failed:
    notify("notify.kbm.save_failed");
    break;
  }
}

/* Lists hand their value to the stock handler as well (as the overlay
   lists do); toggles, text fields and buttons are ours alone. */
bool is_list(const std::string &f) {
  static const char *const kNotLists[] = {"on", "merge", "inherit", "ix", "iy",
                                          "repeat", "name"};
  if (f.rfind("pb", 0) == 0)
    return false;
  for (const char *n : kNotLists)
    if (f == n)
      return false;
  return true;
}

/* The text field keeps what was typed; show the saved name instead. */
void show_saved_text(OnPressContext &ctx, const Path &p) {
  std::string saved;
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : Root_Domain;
  if (!domain)
    domain = Root_Domain;
  if (!get_value(p, saved) || !ctx.element || !domain || !set_value_method ||
      !mono_string_new || !mono_runtime_invoke)
    return;
  MonoString *text = mono_string_new(domain, saved.c_str());
  void *args[] = {text};
  MonoObject *exception = nullptr;
  if (text)
    mono_runtime_invoke(set_value_method, ctx.element, args, &exception);
}

OnPressResult on_press(OnPressContext &ctx) {
  Path p;
  if (!parse_id(ctx.id, &p))
    return OnPressResult::NotMine;
  ctx.dirty = false; /* nothing here lives in config.ini */
  const std::string f = p.word;

  if (f == "input" || f == "suspend") {
    capture_begin(p, ctx.instance);
    return OnPressResult::Consumed;
  }
  if (f == "nomods") {
    if (clear_modifiers(p))
      s_refresh_page = ctx.instance;
    else
      notify("notify.kbm.gone");
    return OnPressResult::Consumed;
  }
  if (f == "newtpl" || f == "newempty" || f == "reset" || f == "clear") {
    create_profile(p, f == "newtpl" || f == "reset");
    notify(f == "newtpl" || f == "reset" ? "notify.kbm.from_template"
                                         : "notify.kbm.emptied");
    s_refresh_page = ctx.instance;
    return OnPressResult::Consumed;
  }
  if (f == "delete") {
    delete_profile(p);
    notify("notify.kbm.profile_deleted");
    s_refresh_page = ctx.instance;
    return OnPressResult::Consumed;
  }
  if (f.rfind("add", 0) == 0) {
    Path created;
    if (add_item(p, f.substr(3), &created))
      toolbox_push_resource(resource(created).c_str());
    else
      notify("notify.kbm.limit");
    return OnPressResult::Consumed;
  }
  if (f.rfind("del", 0) == 0) {
    const int pages = delete_item(p);
    if (pages == 0) {
      notify("notify.kbm.gone");
    } else {
      s_pops = pages;
      s_pop_deadline = now_ms() + kPopGiveUpMs;
      s_deleted_page = ctx.instance;
    }
    return OnPressResult::Consumed;
  }

  const bool saved = set_value(p, ctx.value) && save(p);
  if (!saved && f != "name") /* names explain their own refusal */
    notify("notify.kbm.gone");
  if (f == "name") {
    show_saved_text(ctx, p);
    return OnPressResult::Consumed;
  }
  return is_list(f) ? OnPressResult::Handled : OnPressResult::Consumed;
}

const OnPressPrefixEntry kPrefix[] = {{"id_kbm-", on_press}};

} // namespace

void Tick() {
  capture_tick();
  if (s_refresh_page) {
    MonoObject *page = s_refresh_page;
    s_refresh_page = nullptr;
    onion::shellui::settings::refresh(page);
  }
  /* After a delete, close the deleted item's pages, one transition at a
     time; the page below redraws itself when it comes back. */
  if (s_pops > 0) {
    if (now_ms() > s_pop_deadline) {
      s_pops = 0;
      onion::shellui::settings::refresh(s_deleted_page);
    } else if (!toolbox_is_stack_under_transition() && toolbox_pop_page()) {
      --s_pops;
    }
  }
}

} // namespace kbm_toolbox

const OnPressPrefixEntry *onpress_kbm_prefix(size_t *count) {
  *count = sizeof(kbm_toolbox::kPrefix) / sizeof(kbm_toolbox::kPrefix[0]);
  return kbm_toolbox::kPrefix;
}
