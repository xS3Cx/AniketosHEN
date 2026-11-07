#include "aniketos_shell/system_ui/pin_lock.hpp"

#include "aniketos_shell/system_ui/pin_lock_core.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <ctime>
#include <string>
#include <string_view>
#include <unistd.h>

namespace PinLock {
namespace {

using namespace PinLockCore;

constexpr const char *kRecordPath = ONION_DIST_USER_DATA_ROOT "/pin_lock";
/* Recovery for a forgotten PIN: this file on the first USB drive. */
constexpr const char *kUsbOverride = "/mnt/usb0/no_pin_lock";
constexpr const char *kIcon =
    ONION_DIST_USER_DATA_ROOT "/assets/icon_xml_account.png";
/* Frames between a prompt closing and the next one of the same question. */
constexpr int kChainFrames = 20;

enum class Goal { None, Unlock, Set, Remove };
enum class Step { Current, New, Repeat };

Session s_session;
Goal s_goal = Goal::None;
Step s_step = Step::Current;
void (*s_then)() = nullptr;
uint64_t s_token = 0;
std::string s_new_pin;
int s_chain_frames = 0;
std::string s_locked_resource;
int64_t s_last_frame = 0;
int s_user_check = 0;

const char *tr(const char *key) { return toolbox_i18n::tr(key); }
void say(const char *key) { notify("%s", tr(key)); }
int64_t now() { return static_cast<int64_t>(std::time(nullptr)); }

int foreground_user() {
  int uid = -1;
  return sceUserServiceGetForegroundUser(&uid) == 0 ? uid : -1;
}

bool read_record(std::string &record) {
  return AniketosUI::ReadSmallFile(kRecordPath, 256, record) &&
         !record.empty();
}

/* A PIN is set and the USB override is not there. */
bool enabled() {
  std::string record;
  return read_record(record) && !if_exists(kUsbOverride);
}

void lock(const char *why) {
  if (s_session.unlocked)
    LOG_INFO("[PINLOCK] locked again: %s", why);
  s_session = {};
}

void unlock() { s_session = {true, now(), foreground_user()}; }

bool locked() {
  if (!enabled())
    return false;
  if (s_session.unlocked &&
      !StillOpen(s_session, now(), foreground_user()))
    lock("idle or another user");
  return !s_session.unlocked;
}

void open_step() {
  SystemDialogs::PromptOptions options;
  options.title = tr("pin.group");
  options.message = tr(s_goal == Goal::Unlock     ? "pin.ask.unlock"
                       : s_step == Step::Current ? "pin.ask.current"
                       : s_step == Step::New     ? "pin.ask.new"
                                                 : "pin.ask.repeat");
  options.positive = tr("pkg.msg.ok");
  options.negative = tr("pkg.msg.cancel");
  std::string failure;
  s_token = 0;
  if (!SystemDialogs::ShowPrompt(options, &failure,
                                 SystemDialogs::FocusedScene(), &s_token)) {
    LOG_WARN("[PINLOCK] no prompt: %s", failure.c_str());
    say("pin.prompt_failed");
    s_goal = Goal::None;
  }
}

void start(Goal goal, void (*then)()) {
  if (s_goal != Goal::None && SystemDialogs::PromptPending(s_token))
    return; /* ours is already on screen */
  s_goal = goal;
  s_then = then;
  s_new_pin.clear();
  s_chain_frames = 0;
  s_step = goal == Goal::Set && !enabled() ? Step::New : Step::Current;
  open_step();
}

void remove_record() {
  if (unlink(kRecordPath) != 0 && if_exists(kRecordPath)) {
    say("pin.save_failed");
    return;
  }
  s_session = {};
  LOG_INFO("[PINLOCK] PIN removed");
  say("pin.removed");
}

void save_record(const std::string &pin) {
  timespec real{}, mono{};
  clock_gettime(CLOCK_REALTIME, &real);
  clock_gettime(CLOCK_MONOTONIC, &mono);
  /* The salt only has to differ between consoles and PINs, not be secret. */
  char seed[96];
  std::snprintf(seed, sizeof(seed), "%lld.%ld.%lld.%ld.%d.%p",
                static_cast<long long>(real.tv_sec), real.tv_nsec,
                static_cast<long long>(mono.tv_sec), mono.tv_nsec, getpid(),
                static_cast<void *>(&real));
  const std::string record = MakeRecord(Sha256Hex(seed).substr(0, 32), pin);
  if (!write_file_atomic(kRecordPath, record.data(), record.size())) {
    LOG_WARN("[PINLOCK] cannot write %s", kRecordPath);
    say("pin.save_failed");
    return;
  }
  unlock(); /* whoever set it knows it */
  LOG_INFO("[PINLOCK] PIN saved");
  say("pin.saved");
}

void answer(bool confirmed, const std::string &typed) {
  const Goal goal = s_goal;
  s_goal = Goal::None;
  if (!confirmed)
    return;
  std::string record;
  switch (s_step) {
  case Step::Current:
    if (!read_record(record) || !Matches(record, typed)) {
      LOG_INFO("[PINLOCK] wrong PIN");
      say("pin.wrong");
      return;
    }
    if (goal == Goal::Unlock) {
      unlock();
      LOG_INFO("[PINLOCK] unlocked");
      if (s_then)
        s_then();
    } else if (goal == Goal::Remove) {
      remove_record();
    } else {
      s_goal = goal;
      s_step = Step::New;
      s_chain_frames = kChainFrames;
    }
    return;
  case Step::New:
    if (!ValidPin(typed)) {
      say("pin.invalid");
      return;
    }
    s_new_pin = typed;
    s_goal = goal;
    s_step = Step::Repeat;
    s_chain_frames = kChainFrames;
    return;
  case Step::Repeat:
    if (typed != s_new_pin)
      say("pin.mismatch");
    else
      save_record(typed);
    s_new_pin.clear();
    return;
  }
}

/* After the right PIN on the locked page: open what it stood in for. Pushed
   the way the Toolbox links its own pages (folders with '/'). */
void open_locked_resource() {
  std::string_view relative;
  if (toolbox::settings_plugins_relative(s_locked_resource, &relative)) {
    std::string path(relative);
    const size_t extension = path.rfind('.');
    for (size_t i = 0; extension != std::string::npos && i < extension; ++i)
      if (path[i] == '.')
        path[i] = '/';
    if (toolbox_push_resource(path.c_str()))
      return;
  }
  say("pin.unlocked");
}

} // namespace

void Require(void (*then)()) {
  if (!locked()) {
    if (s_session.unlocked)
      s_session.last_use = now();
    then();
    return;
  }
  start(Goal::Unlock, then);
}

bool Blocks(toolbox::Page page, bool dynamic_page) {
  using toolbox::Page;
  if (!dynamic_page &&
      (page == Page::None || page == Page::SuperuserPass ||
       /* progress of something already started: never cut it off */
       page == Page::CheatProgress || page == Page::PluginProgress ||
       page == Page::PayloadProgress || page == Page::HenUpdateProgress))
    return false;
  if (!locked()) {
    if (s_session.unlocked)
      s_session.last_use = now();
    return false;
  }
  return true;
}

std::string LockedPageXml(const std::string &resource) {
  s_locked_resource = resource;
  /* The prompt opens from the page's own row: opened by itself while
     Settings was still building the page, ShellUI went down. */
  LOG_INFO("[PINLOCK] locked page instead of %s", resource.c_str());
  /* Settings builds the Toolbox root from its root list id: keep it. */
  const bool root = resource.find("debug_settings.xml") != std::string::npos;
  ps5ui::Page page(root ? "id_debug_settings" : "id_pin_locked", tr("root.title"));
  page.root_focus("id_pin_unlock");
  page.label("id_pin_locked_info", tr("pin.locked"), ps5ui::Style::Center)
      .button("id_pin_unlock", tr("pin.unlock"), std::nullopt, std::nullopt,
              kIcon);
  return page.build();
}

void AppendRows(ps5ui::Group &g) {
  g.group(
      "id_pin_lock", tr("pin.group"),
      [](ps5ui::Group &p) {
        p.label("id_pin_about", tr("pin.about"))
            .button("id_pin_set", tr("pin.set"), std::nullopt, std::nullopt,
                    kIcon)
            .button("id_pin_remove", tr("pin.remove"), std::nullopt,
                    std::nullopt, kIcon);
      },
      tr(enabled() ? "vsh.common.on" : "vsh.common.off"), kIcon, "id_pin_set");
}

bool HandlePress(const std::string &id) {
  if (id == "id_pin_unlock") {
    if (locked())
      start(Goal::Unlock, open_locked_resource);
    else
      open_locked_resource();
  } else if (id == "id_pin_set") {
    start(Goal::Set, nullptr);
  } else if (id == "id_pin_remove") {
    std::string record;
    if (!read_record(record))
      say("pin.none");
    else if (!enabled())
      remove_record(); /* the USB override stands in for the PIN */
    else
      start(Goal::Remove, nullptr);
  } else {
    return false;
  }
  return true;
}

void Tick() {
  const int64_t t = now();
  if (s_last_frame && t - s_last_frame > kRestGapSeconds)
    lock("rest mode");
  s_last_frame = t;
  if (s_session.unlocked && ++s_user_check >= 60) {
    s_user_check = 0;
    if (foreground_user() != s_session.user)
      lock("user switch");
  }

  if (s_goal == Goal::None)
    return;
  if (s_chain_frames > 0) {
    if (--s_chain_frames == 0)
      open_step();
    return;
  }
  bool confirmed = false;
  std::string typed;
  if (SystemDialogs::PollPrompt(s_token, &confirmed, &typed))
    answer(confirmed, typed);
  else if (!SystemDialogs::PromptPending(s_token))
    s_goal = Goal::None; /* another prompt took its place */
}

} // namespace PinLock
