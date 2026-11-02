#include "aniketos_shell/games/trophy_tools.hpp"

#include "aniketos_shell/rn_scripts/script_injector.hpp"
#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <ps5/kernel.h>
#include <pthread.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

extern "C" int sceUserServiceGetForegroundUser(int *user_id);

namespace TrophyTools {
namespace {

const char *tr(const char *key) { return toolbox_i18n::tr(key); }

/* ---- the system's layouts (from its managed declarations) -------------- */

struct CommId { /* SceNpCommunicationId: "NPWR12345", the _00 in num */
  char data[9];
  char term;
  uint8_t num;
  char dummy;
};
static_assert(sizeof(CommId) == 12, "SceNpCommunicationId");

struct NpTitleId { /* SceNpTitleId: "PPSA01234_00" */
  char id[13];
  uint8_t padding[3];
};

struct Title1Id { /* SceNpTrophyTitleId (PS4 trophies) */
  char id[33];
};

struct DebugParam { /* SceNpTrophy(2)DebugUnlockParam / LockParam */
  int32_t op;       /* 1 the whole title, 2 a group, 3 one trophy */
  int32_t id;       /* the trophy or group */
};
constexpr int32_t kOpTitle = 1;
constexpr int32_t kOpTrophy = 3;

struct Progress {
  int32_t type;
  uint64_t value;
};

struct Game2Details { /* SceNpTrophy2SystemGameDetails */
  uint32_t groups, trophies, platinum, gold, silver, bronze;
  uint32_t platforms;
  char title[128];
};

struct Trophy2Details { /* SceNpTrophy2SystemTrophyDetails */
  int32_t trophy_id, grade, group_id;
  bool hidden, has_reward, has_background;
  uint8_t reserved;
  Progress target;
  char name[128];
  char description[1024];
  char reward[128];
};
static_assert(offsetof(Trophy2Details, name) == 32, "trophy2 details");

struct Title1Details { /* SceNpTrophySystemWrapTitleDetails */
  Title1Id id;
  char name[128];
  char description[1024];
  char icon[128];
  uint32_t platforms, groups;
  int32_t trophies, platinum, gold, silver, bronze;
  bool has_group;
};

struct Trophy1Details { /* SceNpTrophySystemWrapTrophyDetails */
  int32_t trophy_id, group_id;
  char name[128];
  char description[1024];
  char icon[128];
  int32_t grade;
  bool hidden;
};

/* ---- the calls, found by name in the system's trophy libraries -------- */

struct Api {
  bool ready = false;
  int (*create_handle)(int *) = nullptr;
  int (*destroy_handle)(int) = nullptr;
  int (*create_context)(int *, int, const void *, uint64_t) = nullptr;
  int (*destroy_context)(int) = nullptr;
  int (*titles_by_np_title)(int, const NpTitleId *, uint64_t, void *,
                            uint64_t *) = nullptr;
  int (*title_details)(int, int, void *) = nullptr;
  int (*trophy_details)(int, int, int, void *) = nullptr;
  /* PS5: (handle, context, param); PS4 also reports the platinum it changed */
  int (*unlock2)(int, int, const DebugParam *) = nullptr;
  int (*lock2)(int, int, const DebugParam *) = nullptr;
  int (*unlock1)(int, int, const DebugParam *, int32_t *) = nullptr;
  int (*lock1)(int, int, const DebugParam *, int32_t *) = nullptr;
};
Api s_ps5, s_ps4;

/* 1 when found; every name is looked up, so each missing one is logged. */
template <typename F>
int find(uint32_t module, const char *name, F *function) {
  const intptr_t address = kernel_dynlib_dlsym(-1, module, name);
  *function = address > 0 ? reinterpret_cast<F>(address) : nullptr;
  if (!*function)
    LOG_WARN("[TROPHY] %s not found", name);
  return *function != nullptr ? 1 : 0;
}

void resolve() {
  static bool done = false;
  if (done)
    return;
  done = true;
  uint32_t module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceNpTrophy2.sprx", &module) == 0) {
    Api &a = s_ps5;
    const int found =
        find(module, "sceNpTrophy2SystemCreateHandle", &a.create_handle) &
        find(module, "sceNpTrophy2SystemDestroyHandle", &a.destroy_handle) &
        find(module, "sceNpTrophy2SystemCreateContext", &a.create_context) &
        find(module, "sceNpTrophy2SystemDestroyContext", &a.destroy_context) &
        find(module, "sceNpTrophy2SystemListTitlesByNpTitleId",
             &a.titles_by_np_title) &
        find(module, "sceNpTrophy2SystemGetTrophyTitleDetails",
             &a.title_details) &
        find(module, "sceNpTrophy2SystemGetTrophyDetails", &a.trophy_details) &
        find(module, "sceNpTrophy2SystemDebugUnlockTrophy", &a.unlock2) &
        find(module, "sceNpTrophy2SystemDebugLockTrophy", &a.lock2);
    a.ready = found != 0;
  }
  module = 0;
  if (kernel_dynlib_handle(getpid(), "libSceNpTrophy.sprx", &module) == 0) {
    Api &a = s_ps4;
    const int found =
        find(module, "sceNpTrophySystemCreateHandle", &a.create_handle) &
        find(module, "sceNpTrophySystemDestroyHandle", &a.destroy_handle) &
        find(module, "sceNpTrophySystemCreateContext", &a.create_context) &
        find(module, "sceNpTrophySystemDestroyContext", &a.destroy_context) &
        find(module, "sceNpTrophySystemWrapGetTrophyTitleIdsByNpTitleId",
             &a.titles_by_np_title) &
        find(module, "sceNpTrophySystemWrapGetTitleDetails", &a.title_details) &
        find(module, "sceNpTrophySystemWrapGetTrophyDetails",
             &a.trophy_details) &
        find(module, "sceNpTrophySystemWrapDebugUnlockTrophy", &a.unlock1) &
        find(module, "sceNpTrophySystemWrapDebugLockTrophy", &a.lock1);
    a.ready = found != 0;
  }
  LOG_INFO("[TROPHY] calls ps5=%d ps4=%d", s_ps5.ready ? 1 : 0,
           s_ps4.ready ? 1 : 0);
}

/* ---- one trophy set ---------------------------------------------------- */

/* A trophy set: PS5 ("trophy2") or PS4 ("trophy1"), by its NPWR id. */
struct Set {
  bool ps5 = true;
  std::string id; /* NPWR12345_00 */
};

int foreground_user() {
  int user = -1;
  sceUserServiceGetForegroundUser(&user);
  return user;
}

/* A handle and a context for `set`, given back when it goes. */
class Session {
public:
  Session(const Set &set, int user) : api_(set.ps5 ? s_ps5 : s_ps4) {
    if (!api_.ready) {
      error_ = -1;
      return;
    }
    error_ = api_.create_handle(&handle_);
    if (error_ != 0) {
      handle_ = -1;
      return;
    }
    if (set.ps5) {
      CommId comm{};
      std::memcpy(comm.data, set.id.data(), std::min<size_t>(9, set.id.size()));
      const size_t cut = set.id.find('_');
      comm.num = cut == std::string::npos
                     ? 0
                     : static_cast<uint8_t>(std::atoi(set.id.c_str() + cut + 1));
      error_ = api_.create_context(&context_, user, &comm, 0);
    } else {
      Title1Id title{};
      std::snprintf(title.id, sizeof(title.id), "%s", set.id.c_str());
      error_ = api_.create_context(&context_, user, &title, 0);
    }
    open_ = error_ == 0;
  }
  ~Session() {
    if (open_)
      api_.destroy_context(context_);
    if (handle_ >= 0)
      api_.destroy_handle(handle_);
  }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool ok() const { return open_; }
  int error() const { return error_; }
  int handle() const { return handle_; }
  int context() const { return context_; }

private:
  const Api &api_;
  int handle_ = -1;
  int context_ = -1;
  int error_ = 0;
  bool open_ = false;
};

/* Big enough for either platform's details; the worker is the only user. */
union DetailsBuffer {
  Game2Details game2;
  Title1Details title1;
  Trophy2Details trophy2;
  Trophy1Details trophy1;
};
DetailsBuffer s_details;

/* The set's name and trophy count; empty when they cannot be read. */
std::string set_name(const Set &set, const Session &session, int *count) {
  std::memset(&s_details, 0, sizeof(s_details));
  const Api &api = set.ps5 ? s_ps5 : s_ps4;
  if (api.title_details(session.handle(), session.context(), &s_details) != 0)
    return {};
  if (set.ps5) {
    *count = static_cast<int>(s_details.game2.trophies);
    return std::string(s_details.game2.title,
                       strnlen(s_details.game2.title, sizeof(s_details.game2.title)));
  }
  *count = s_details.title1.trophies;
  return std::string(s_details.title1.name,
                     strnlen(s_details.title1.name, sizeof(s_details.title1.name)));
}

std::string trophy_name(const Set &set, const Session &session, int trophy) {
  std::memset(&s_details, 0, sizeof(s_details));
  const Api &api = set.ps5 ? s_ps5 : s_ps4;
  if (api.trophy_details(session.handle(), session.context(), trophy,
                         &s_details) == 0) {
    const char *name =
        set.ps5 ? s_details.trophy2.name : s_details.trophy1.name;
    return std::string(name, strnlen(name, 128));
  }
  return "#" + std::to_string(trophy);
}

/* Unlocks (or locks) one trophy, or the whole set when `trophy` < 0. */
int change(const Set &set, const Session &session, bool unlock, int trophy) {
  const DebugParam param{trophy < 0 ? kOpTitle : kOpTrophy,
                         trophy < 0 ? 0 : trophy};
  if (set.ps5)
    return unlock ? s_ps5.unlock2(session.handle(), session.context(), &param)
                  : s_ps5.lock2(session.handle(), session.context(), &param);
  int32_t platinum = -1;
  return unlock ? s_ps4.unlock1(session.handle(), session.context(), &param,
                                &platinum)
                : s_ps4.lock1(session.handle(), session.context(), &param,
                              &platinum);
}

/* The trophy sets of a game, from its title id (CUSA12345, PPSA01234). */
std::vector<Set> sets_of(const std::string &title_id) {
  std::vector<Set> sets;
  NpTitleId np{};
  std::snprintf(np.id, sizeof(np.id), "%.9s_00", title_id.c_str());
  for (const bool ps5 : {true, false}) {
    const Api &api = ps5 ? s_ps5 : s_ps4;
    if (!api.ready)
      continue;
    int handle = -1;
    if (api.create_handle(&handle) != 0)
      continue;
    uint64_t count = 8;
    if (ps5) {
      CommId ids[16] = {};
      if (api.titles_by_np_title(handle, &np, 0, ids, &count) == 0)
        for (uint64_t i = 0; i < count && i < 8; ++i) {
          char text[16];
          std::snprintf(text, sizeof(text), "%.9s_%02u", ids[i].data,
                        static_cast<unsigned>(ids[i].num));
          sets.push_back({true, text});
        }
    } else {
      Title1Id ids[16] = {};
      if (api.titles_by_np_title(handle, &np, 0, ids, &count) == 0)
        for (uint64_t i = 0; i < count && i < 8; ++i)
          sets.push_back(
              {false, std::string(ids[i].id, strnlen(ids[i].id, sizeof(ids[i].id)))});
    }
    api.destroy_handle(handle);
  }
  LOG_INFO("[TROPHY] %s: %zu trophy set(s)", np.id, sets.size());
  return sets;
}

/* ---- work, done on a thread of its own ---------------------------------- */

enum class Job { Change, PrepareAll };

struct Work {
  Job job = Job::Change;
  bool unlock = true;
  std::vector<Set> sets;
  int user = -1;
  int trophy = -1;       /* < 0: the whole set */
  std::string title_id;  /* PrepareAll */
};

/* What the UI thread shows once the work is done. */
struct Outcome {
  std::string key;      /* the notification, or empty */
  std::string text;     /* its %s */
  int error = 0;        /* the failure notification's code */
  bool confirm = false; /* PrepareAll: ask before unlocking `work` */
  Work work;
  int count = 0;
};

pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_wake = PTHREAD_COND_INITIALIZER;
std::deque<Work> s_work;
std::deque<Outcome> s_outcomes;
bool s_thread_started = false;

void post(Outcome outcome) {
  pthread_mutex_lock(&s_lock);
  s_outcomes.push_back(std::move(outcome));
  pthread_mutex_unlock(&s_lock);
}

Outcome notice(const char *key, const std::string &text, int error = 0) {
  Outcome outcome;
  outcome.key = key;
  outcome.text = text;
  outcome.error = error;
  return outcome;
}

void run(const Work &work) {
  resolve();
  const int user = work.user > 0 ? work.user : foreground_user();
  if (work.job == Job::PrepareAll) {
    Outcome outcome;
    outcome.work.unlock = true;
    outcome.work.user = user;
    outcome.work.sets = sets_of(work.title_id);
    if (outcome.work.sets.empty()) {
      post(notice("notify.trophy.not_found", work.title_id));
      return;
    }
    std::string name;
    {
      Session session(outcome.work.sets.front(), user);
      if (session.ok())
        name = set_name(outcome.work.sets.front(), session, &outcome.count);
    }
    outcome.text = name.empty() ? work.title_id : name;
    outcome.confirm = true;
    post(std::move(outcome));
    return;
  }
  for (const Set &set : work.sets) {
    Session session(set, user);
    if (!session.ok()) {
      LOG_WARN("[TROPHY] %s: no context (0x%x)", set.id.c_str(),
               static_cast<unsigned>(session.error()));
      post(notice("notify.trophy.failed", set.id, session.error()));
      continue;
    }
    int count = 0;
    const std::string name = work.trophy < 0
                                 ? set_name(set, session, &count)
                                 : trophy_name(set, session, work.trophy);
    const int result = change(set, session, work.unlock, work.trophy);
    LOG_INFO("[TROPHY] %s %s %s trophy=%d user=0x%x -> 0x%x",
             work.unlock ? "unlock" : "lock", set.ps5 ? "ps5" : "ps4",
             set.id.c_str(), work.trophy, static_cast<unsigned>(user),
             static_cast<unsigned>(result));
    const std::string shown = name.empty() ? set.id : name;
    if (result != 0) {
      post(notice("notify.trophy.failed", shown, result));
      continue;
    }
    post(notice(work.trophy < 0 ? (work.unlock ? "notify.trophy.unlocked_all"
                                               : "notify.trophy.locked_all")
                                : (work.unlock ? "notify.trophy.unlocked"
                                               : "notify.trophy.locked"),
                shown));
  }
}

void *worker(void *) {
  for (;;) {
    pthread_mutex_lock(&s_lock);
    while (s_work.empty())
      pthread_cond_wait(&s_wake, &s_lock);
    Work work = std::move(s_work.front());
    s_work.pop_front();
    pthread_mutex_unlock(&s_lock);
    run(work);
  }
  return nullptr;
}

void queue(Work work) {
  pthread_mutex_lock(&s_lock);
  if (!s_thread_started) {
    pthread_t thread{};
    s_thread_started = pthread_create(&thread, nullptr, worker, nullptr) == 0;
    if (s_thread_started)
      pthread_detach(thread);
  }
  s_work.push_back(std::move(work));
  pthread_cond_signal(&s_wake);
  pthread_mutex_unlock(&s_lock);
}

/* ---- the confirmation for "Unlock all trophies" on Home ---------------- */

bool s_confirm_open = false;
Work s_confirm_work;

void show(const Outcome &outcome) {
  if (outcome.confirm) {
    if (s_confirm_open)
      return; /* one at a time */
    SystemDialogs::AlertOptions options;
    options.title = tr("trophy.confirm.unlock_all");
    char count[16];
    std::snprintf(count, sizeof(count), "%d", outcome.count);
    options.message = outcome.text + "\n" + tr("trophy.confirm.count") + " " +
                      count + "\n\n" + tr("trophy.confirm.sync_note");
    options.positive = tr("trophy.confirm.unlock");
    options.negative = tr("trophy.confirm.cancel");
    std::string failure;
    if (SystemDialogs::ShowAlert(options, &failure)) {
      s_confirm_open = true;
      s_confirm_work = outcome.work;
    } else {
      LOG_WARN("[TROPHY] confirmation not shown: %s", failure.c_str());
    }
    return;
  }
  if (outcome.error != 0)
    notify(outcome.key.c_str(), outcome.text.c_str(),
           static_cast<unsigned>(outcome.error));
  else
    notify(outcome.key.c_str(), outcome.text.c_str());
}

} // namespace

void Tick() {
  if (s_confirm_open) {
    int button = -1;
    if (SystemDialogs::PollAlert(&button)) {
      s_confirm_open = false;
      if (button == 0)
        queue(s_confirm_work);
    }
  }
  if (!s_thread_started)
    return;
  pthread_mutex_lock(&s_lock);
  if (s_outcomes.empty()) {
    pthread_mutex_unlock(&s_lock);
    return;
  }
  Outcome outcome = std::move(s_outcomes.front());
  s_outcomes.pop_front();
  pthread_mutex_unlock(&s_lock);
  show(outcome);
}

void RequestUnlockAll(const std::string &title_id) {
  if (title_id.size() < 9) {
    LOG_WARN("[TROPHY] unlock all: no title id ('%s')", title_id.c_str());
    return;
  }
  Work work;
  work.job = Job::PrepareAll;
  work.title_id = title_id;
  queue(std::move(work));
}

void HandleScriptMessage(const std::string &message) {
  /* op|type|id|user|trophy */
  std::vector<std::string> parts;
  size_t from = 0;
  for (;;) {
    const size_t bar = message.find('|', from);
    parts.push_back(message.substr(from, bar == std::string::npos
                                             ? std::string::npos
                                             : bar - from));
    if (bar == std::string::npos)
      break;
    from = bar + 1;
  }
  if (parts.size() < 5 || (parts[1] != "trophy1" && parts[1] != "trophy2") ||
      parts[2].size() < 9 || parts[2].size() > 32) {
    LOG_WARN("[TROPHY] request not understood: %s", message.c_str());
    return;
  }
  const std::string &op = parts[0];
  Work work;
  work.job = Job::Change;
  work.sets.push_back({parts[1] == "trophy2", parts[2]});
  work.user = std::atoi(parts[3].c_str());
  if (op == "unlock" || op == "lock") {
    work.unlock = op == "unlock";
    work.trophy = std::atoi(parts[4].c_str());
    if (work.trophy < 0)
      return;
  } else if (op == "unlockall" || op == "lockall") {
    work.unlock = op == "unlockall";
    work.trophy = -1;
  } else {
    LOG_WARN("[TROPHY] unknown request '%s'", op.c_str());
    return;
  }
  queue(std::move(work));
}

const char *InjectedScript() {
  static const char script[] =
#include "../rn_scripts/trophy_inject_js.inc"
      ;
  return script;
}

std::string ScriptConfig() {
  const auto text = [](const char *key) { return HomeJs::JsString(tr(key)); };
  return "globalThis.__aniketosTrophy = {unlock: " + text("trophy.unlock") +
         ", lock: " + text("trophy.lock") +
         ", unlockAll: " + text("trophy.unlock_all") +
         ", lockAll: " + text("trophy.lock_all") +
         ", unlockAllTitle: " + text("trophy.confirm.unlock_all") +
         ", lockAllTitle: " + text("trophy.confirm.lock_all") +
         ", unlockYes: " + text("trophy.confirm.unlock") +
         ", lockYes: " + text("trophy.confirm.lock") +
         ", cancel: " + text("trophy.confirm.cancel") +
         ", syncNote: " + text("trophy.confirm.sync_note") + "};";
}

} // namespace TrophyTools
