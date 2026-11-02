/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Keyboard & mouse for games, daemon side. A started game is remembered; the
 * service thread waits for its pad library, injects the agent, waits for the
 * agent's control page and writes the compiled profile into it. A newer
 * lifecycle event (exit, another start) cancels any wait in progress.
 */

#include "kbm_service.hpp"

#include <onion/fps_formula.hpp>
#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/kbm_store.hpp>
#include <onion/log.h>
#include <onion/proc_dmap.h>
#include <onion/proc_query.h>
#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>

extern "C" {
bool Inject_Toolbox(int pid, uint8_t *elf);
extern const uint8_t kbm_agent_elf_start[];
extern const int kbm_agent_elf_size;
}

namespace onion::daemon {

std::mutex &inject_mutex() {
  static std::mutex mutex;
  return mutex;
}

namespace kbm {
namespace {

namespace store = ::onion::kbm;
using store::ControlPage;

constexpr int kPollMs = 250;
constexpr int kPadWaitMs = 90 * 1000; /* big games load slowly from disc */
constexpr int kSettleMs = 5 * 1000;   /* let the game finish its own start */
constexpr int kAgentWaitMs = 10 * 1000;
/* A game that ends this soon after the agent went in is left alone from
   then on (until its profile is saved again): no crash loop. */
constexpr time_t kCrashWindowS = 30;
constexpr const char *kKillFile = ONION_DIST_DATA_ROOT "/no_kbm";

struct Game {
  pid_t pid = -1;
  std::string title_id;
};

std::mutex s_game_mutex;
std::condition_variable s_game_changed;
Game s_current_game;           /* the running game; pid -1 when none */
uint32_t s_game_generation = 0; /* bumped on every start/exit: cancels waits */
bool s_serve_pending = false;  /* start or reload: serve s_current_game */
pid_t s_injected_pid = -1;     /* the last game the agent went into ... */
std::string s_injected_title;
time_t s_injected_at = 0;      /* ... and when */

/* Wait up to `ms`; false once a lifecycle event replaced this job. */
bool nap(uint32_t gen, int ms) {
  std::unique_lock<std::mutex> lock(s_game_mutex);
  return !s_game_changed.wait_for(lock, std::chrono::milliseconds(ms),
                          [gen] { return s_game_generation != gen; });
}

template <typename Ready>
bool wait_until(const Game &game, uint32_t gen, int timeout_ms, Ready ready) {
  for (int waited = 0; waited < timeout_ms; waited += kPollMs) {
    if (ready())
      return true;
    if (!isProcessAlive(game.pid) || !nap(gen, kPollMs))
      return false;
  }
  return ready();
}

bool sprx_loaded(pid_t pid, const char *name) {
  uint32_t handle = static_cast<uint32_t>(-1);
  return kernel_dynlib_handle(pid, name, &handle) == 0 &&
         handle != static_cast<uint32_t>(-1);
}

/* mdbg faults in pages the agent has not touched yet; the DMAP route is the
   fallback where mdbg is refused. */
bool game_read(pid_t pid, uint64_t addr, void *buf, size_t len) {
  return mdbg_copyout(pid, static_cast<intptr_t>(addr), buf, len) == 0 ||
         onion_proc_copyout(pid, addr, buf, len) == 0;
}

bool game_write(pid_t pid, uint64_t addr, const void *buf, size_t len) {
  return mdbg_copyin(pid, buf, static_cast<intptr_t>(addr), len) == 0 ||
         onion_proc_copyin(pid, addr, buf, len) == 0;
}

/* The agent's control page in the game: a read-write anonymous mapping
   starting with its magic and layout (the agent picks the address). */
uint64_t find_page(pid_t pid) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, static_cast<int>(pid)};
  size_t size = 0;
  if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || !size)
    return 0;
  size += 16 * 1024;
  std::unique_ptr<char[]> buf(new (std::nothrow) char[size]);
  if (!buf || sysctl(mib, 4, buf.get(), &size, nullptr, 0) != 0)
    return 0;
  for (char *p = buf.get(); p < buf.get() + size;) {
    const auto *e = reinterpret_cast<const kinfo_vmentry *>(p);
    if (e->kve_structsize <= 0)
      break;
    p += e->kve_structsize;
    if ((e->kve_protection & 3) != 3 || (e->kve_protection & 4) ||
        e->kve_end - e->kve_start < sizeof(ControlPage))
      continue;
    struct {
      uint64_t magic;
      uint32_t layout_version;
      uint32_t page_size;
    } head{};
    if (game_read(pid, e->kve_start, &head, sizeof(head)) &&
        head.magic == store::kControlMagic &&
        head.layout_version == store::kControlLayoutVersion &&
        head.page_size >= sizeof(ControlPage))
      return e->kve_start;
  }
  return 0;
}

/* Which agent build runs: a checksum of the ELF we inject. */
uint32_t agent_checksum() {
  static const uint32_t value = [] {
    uint32_t h = 2166136261u; /* FNV-1a */
    for (int i = 0; i < kbm_agent_elf_size; ++i)
      h = (h ^ kbm_agent_elf_start[i]) * 16777619u;
    return h ? h : 1u;
  }();
  return value;
}

time_t mtime(const std::string &path) {
  struct stat st{};
  return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

std::string block_path(const std::string &title_id) {
  return std::string(store::kStoreRoot) + "/blocked_" + title_id;
}

/* Blocked after an early exit, until its profile is saved again. */
bool blocked(const std::string &title_id) {
  const time_t since = mtime(block_path(title_id));
  if (!since)
    return false;
  const std::string profile = store::TitleProfilePath(store::kStoreRoot, title_id);
  if (mtime(profile) > since || mtime(store::DefaultProfilePath(store::kStoreRoot)) > since) {
    unlink(block_path(title_id).c_str());
    return false;
  }
  return true;
}

/* PS5 titles only: the agent has only been proven in PS5 games. */
bool is_game(const std::string &title_id) {
  return onion::fps::is_ps5_native_title(title_id.c_str());
}

bool inject_agent(const Game &game, uint32_t gen, bool replacing) {
  const pid_t pid = game.pid;
  if (!wait_until(game, gen, kPadWaitMs,
                  [pid] { return sprx_loaded(pid, "libScePad.sprx"); })) {
    LOG_INFO("[kbm] %s pid=%d: no libScePad (or the game left); not injected",
             game.title_id.c_str(), static_cast<int>(pid));
    return false;
  }
  if (!nap(gen, kSettleMs) || !isProcessAlive(pid))
    return false;

  /* shortcut: a game with its own KB+M is always left alone (checked before
     our first injection only: the agent itself loads libSceMouse); a profile
     switch to force it needs a Profile field (contract change). */
  if (!replacing && (sprx_loaded(pid, "libSceMouse.sprx") ||
                     sprx_loaded(pid, "libSceKeyboard.sprx"))) {
    LOG_INFO("[kbm] %s pid=%d reads the mouse/keyboard itself; not injected",
             game.title_id.c_str(), static_cast<int>(pid));
    return false;
  }

  bool injected;
  {
    /* libNineS keeps global ptrace state and swaps our authid while it
       works: one injection at a time across the daemon. */
    std::lock_guard<std::mutex> lock(inject_mutex());
    {
      /* before it goes in: a game that dies during the injection counts */
      std::lock_guard<std::mutex> game_lock(s_game_mutex);
      s_injected_pid = pid;
      s_injected_title = game.title_id;
      s_injected_at = time(nullptr);
    }
    injected = Inject_Toolbox(static_cast<int>(pid),
                              const_cast<uint8_t *>(kbm_agent_elf_start));
  }
  if (!injected) {
    LOG_ERROR("[kbm] %s pid=%d: agent injection failed", game.title_id.c_str(),
              static_cast<int>(pid));
    if (isProcessAlive(pid)) {
      std::lock_guard<std::mutex> lock(s_game_mutex);
      s_injected_pid = -1;
    }
    return false;
  }
  uint64_t page = 0;
  if (!wait_until(game, gen, kAgentWaitMs,
                  [pid, &page] { return (page = find_page(pid)) != 0; })) {
    LOG_ERROR("[kbm] %s pid=%d: agent injected but its control page never "
              "appeared",
              game.title_id.c_str(), static_cast<int>(pid));
    return false;
  }
  const uint32_t build = agent_checksum();
  game_write(pid, page + offsetof(ControlPage, agent_build), &build, sizeof(build));
  LOG_INFO("[kbm] %s pid=%d: agent running, control page 0x%lx",
           game.title_id.c_str(), static_cast<int>(pid),
           static_cast<unsigned long>(page));
  return true;
}

bool push_profile(const Game &game, uint64_t page,
                  const store::Settings &settings) {
  store::Warnings warnings;
  const store::Profile profile =
      store::ResolveProfile(store::kStoreRoot, game.title_id, &warnings);
  auto blob = std::make_unique<store::ProfileBlob>(); /* ~40 KB: off stack */
  store::Compile(profile, blob.get(), settings.suspend_key, &warnings);
  for (const std::string &warning : warnings)
    LOG_DEBUG("[kbm] %s profile: %s", game.title_id.c_str(), warning.c_str());
  if (!settings.enabled)
    blob->enabled = 0; /* switched off mid-game: the agent passes the pad */

  /* seqlock: odd while the profile is being written, even once whole */
  const uint64_t gen_at = page + offsetof(ControlPage, profile_generation);
  uint32_t generation = 0;
  if (!game_read(game.pid, gen_at, &generation, sizeof(generation))) {
    LOG_ERROR("[kbm] %s pid=%d: control page unreadable", game.title_id.c_str(),
              static_cast<int>(game.pid));
    return false;
  }
  generation = (generation | 1u) + ((generation & 1u) ? 2u : 0u);
  if (!game_write(game.pid, gen_at, &generation, sizeof(generation)) ||
      !game_write(game.pid, page + offsetof(ControlPage, profile), blob.get(),
                  sizeof(*blob))) {
    LOG_ERROR("[kbm] %s pid=%d: profile write failed", game.title_id.c_str(),
              static_cast<int>(game.pid));
    return false;
  }
  ++generation; /* even: the agent may copy it now */
  if (!game_write(game.pid, gen_at, &generation, sizeof(generation))) {
    LOG_ERROR("[kbm] %s pid=%d: profile generation write failed",
              game.title_id.c_str(), static_cast<int>(game.pid));
    return false;
  }
  LOG_INFO("[kbm] %s pid=%d: profile \"%s\" applied (gen %u, %u bindings%s)",
           game.title_id.c_str(), static_cast<int>(game.pid),
           profile.name.c_str(), generation,
           static_cast<unsigned>(blob->binding_count),
           blob->enabled ? "" : ", off");
  return true;
}

void serve(const Game &game, uint32_t gen) {
  const store::Settings settings = store::LoadSettings(store::kStoreRoot);
  uint64_t page = find_page(game.pid);
  const bool wanted = settings.enabled && !if_exists(kKillFile) &&
                      store::TitleEnabled(store::kStoreRoot, game.title_id) &&
                      !blocked(game.title_id);
  if (!page) {
    if (!wanted) {
      LOG_DEBUG("[kbm] %s pid=%d: not for this game", game.title_id.c_str(),
                static_cast<int>(game.pid));
      return;
    }
    if (!inject_agent(game, gen, /*replacing=*/false))
      return;
    page = find_page(game.pid);
    if (!page)
      return;
  } else if (wanted) {
    /* an agent from an older HEN: the one we carry takes over from it */
    uint32_t build = 0;
    if (game_read(game.pid, page + offsetof(ControlPage, agent_build), &build,
                  sizeof(build)) &&
        build != agent_checksum()) {
      LOG_INFO("[kbm] %s pid=%d: replacing an older agent",
               game.title_id.c_str(), static_cast<int>(game.pid));
      (void)inject_agent(game, gen, /*replacing=*/true);
      page = find_page(game.pid);
      if (!page)
        return;
    }
  }
  store::Settings effective = settings;
  if (!wanted)
    effective.enabled = false; /* the agent stays, passing the pad through */
  (void)push_profile(game, page, effective);
}

void *worker(void *) {
  std::unique_lock<std::mutex> lock(s_game_mutex);
  for (;;) {
    s_game_changed.wait(lock, [] { return s_serve_pending; });
    s_serve_pending = false;
    const Game game = s_current_game;
    const uint32_t gen = s_game_generation;
    lock.unlock();
    if (game.pid > 1)
      serve(game, gen);
    lock.lock();
  }
  return nullptr;
}

/* Callers hold s_game_mutex. */
void ensure_worker_locked() {
  static bool started = false;
  if (started)
    return;
  pthread_t thread = nullptr;
  started = pthread_create(&thread, nullptr, worker, nullptr) == 0;
  if (started)
    pthread_detach(thread);
  else
    LOG_ERROR("[kbm] service thread creation failed");
}

class Subscriber final : public onion::lifecycle::ISubscriber {
public:
  /* first: nothing slow here, and a failing AppJailbreak (10) stops dispatch */
  int priority() const noexcept override { return 5; }

  bool on_event(const onion::lifecycle::Event &event) noexcept override {
    const onion::lifecycle::BigApp &app = event.app;
    std::lock_guard<std::mutex> lock(s_game_mutex);
    if (event.type == onion::lifecycle::EventType::BigAppExited) {
      if (app.pid == s_injected_pid &&
          time(nullptr) - s_injected_at < kCrashWindowS) {
        /* touched only by this thread and the worker under s_game_mutex */
        const std::string path = block_path(s_injected_title);
        if (FILE *f = fopen(path.c_str(), "w"))
          fclose(f);
        LOG_WARN("[kbm] %s ended %lds after the agent went in: KB+M is off "
                 "for it until its profile is saved again",
                 s_injected_title.c_str(),
                 static_cast<long>(time(nullptr) - s_injected_at));
      }
      if (app.pid == s_injected_pid)
        s_injected_pid = -1;
      if (s_current_game.pid == app.pid) {
        s_current_game = Game{};
        ++s_game_generation;
        s_game_changed.notify_all();
      }
      return true;
    }
    ++s_game_generation; /* a new Big App always cancels the previous one's waits */
    if (!is_game(app.title_id)) {
      s_current_game = Game{};
      s_game_changed.notify_all();
      return true;
    }
    s_current_game = Game{app.pid, app.title_id};
    s_serve_pending = true;
    ensure_worker_locked();
    s_game_changed.notify_all();
    return true;
  }
};

} // namespace

onion::lifecycle::ISubscriber &subscriber() {
  static Subscriber value;
  return value;
}

void request_reload() {
  std::lock_guard<std::mutex> lock(s_game_mutex);
  if (s_current_game.pid <= 1) {
    LOG_DEBUG("[kbm] reload: no game running");
    return;
  }
  s_serve_pending = true;
  ensure_worker_locked();
  s_game_changed.notify_all();
}

} // namespace kbm
} // namespace onion::daemon
