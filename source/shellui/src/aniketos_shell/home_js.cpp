#include "aniketos_shell/home_js.hpp"
#include "aniketos_theme/customization.hpp"

#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "aniketos_shell/content_info.hpp"
#include "aniketos_shell/home_bookmark.hpp"
#include "aniketos_shell/home_menu.hpp"
#include "aniketos_shell/home_tabs.hpp"
#include "aniketos_shell/trophy_tools.hpp"
#include "shell_clock.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern onion::Settings g_settings;

namespace HomeJs {
namespace {

/* Present while an injection is in flight. If ShellUI dies inside it the
   file stays, and the next start leaves the JavaScript side alone instead
   of dying the same way again. */
constexpr const char *kPending = ONION_DIST_USER_DATA_ROOT "/js_inject_pending";
/* Present from an injection until the script reports, eight seconds on,
   that Home is running with it. Still there at the next start: Home did not
   get that far. It holds how many starts in a row that has happened, since
   one alone is as likely a HEN reload or a power-off inside those eight
   seconds as a script that brings Home down, and the pause that follows:
   after kUnconfirmedStartsAllowed such starts the script is left out for
   `left` ShellUI starts, then tried again, each further failure doubling
   the pause (up to kPauseMax). So a script that keeps bringing Home down
   costs one Home restart in many, and two restarts for some other reason
   (a HEN reload, a power cut, another module) cost one start, not the
   script for good. "<count> <pause> <left>"; an older file holds the count
   alone. The daemon removes it before a restart it asks for itself. */
constexpr const char *kUnconfirmed =
    ONION_DIST_USER_DATA_ROOT "/js_inject_unconfirmed";
/* Starts in a row that may end unconfirmed before the script is left out. */
constexpr int kUnconfirmedStartsAllowed = 2;
constexpr int kPauseMax = 32;
struct Unconfirmed {
  int count = 0; /* starts in a row that never confirmed the script */
  int pause = 1; /* starts to leave the script out after the next failure */
  int left = 0;  /* starts still to leave it out now */
};
/* Create this file to keep the JavaScript side untouched. */
constexpr const char *kOff = ONION_DIST_USER_DATA_ROOT "/no_home_js";

using RunScript = void (*)(MonoObject *, MonoObject *);
RunScript s_run_script_original = nullptr;
using ConsoleLog = void (*)(MonoString *, int, void *, void *);
ConsoleLog s_console_log_original = nullptr;
MonoClass *s_executor_class = nullptr;
bool s_disabled = false;
/* The record kept in kUnconfirmed, as read at this start. */
Unconfirmed s_unconfirmed;
/* An app (Home, the Control Center) with the script in it, not yet
   reported running. An app that falls over starts again within a second,
   inside the same ShellUI, so the markers above never see it: after
   kRestartsAllowed such restarts the script stays out until ShellUI starts
   again. */
struct AppStarts {
  bool unconfirmed = false;
  int restarts = 0;
};
AppStarts s_home_starts;
AppStarts s_control_center_starts;
constexpr int kRestartsAllowed = 2;

void note_app_start(AppStarts &starts, const char *app) {
  if (starts.unconfirmed && ++starts.restarts >= kRestartsAllowed &&
      !s_disabled) {
    s_disabled = true;
    LOG_WARN("[HOMEJS] %s started %d times over before it was running with "
             "the script; it stays out until ShellUI starts again",
             app, starts.restarts);
  }
}

std::string text_of(MonoObject *managed) {
  if (!managed)
    return {};
  const char *utf = mono_string_to_utf8(reinterpret_cast<MonoString *>(managed));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
}

std::string describe(MonoObject *object) {
  MonoObject *exception = nullptr;
  MonoString *shown = object ? mono_object_to_string(object, &exception)
                             : nullptr;
  return shown && !exception ? text_of(reinterpret_cast<MonoObject *>(shown))
                             : std::string();
}

bool exists(const char *path) {
  struct stat st {};
  return stat(path, &st) == 0;
}

/* The record in kUnconfirmed; a file written before it held one counts once. */
Unconfirmed read_unconfirmed() {
  Unconfirmed record;
  const int fd = open(kUnconfirmed, O_RDONLY);
  if (fd < 0)
    return record;
  char text[48] = {};
  const ssize_t length = read(fd, text, sizeof(text) - 1);
  close(fd);
  int count = 0, pause = 1, left = 0;
  const int fields =
      length > 0 ? std::sscanf(text, "%d %d %d", &count, &pause, &left) : 0;
  record.count = fields >= 1 && count > 0 ? count : 1;
  record.pause = fields >= 2 ? std::clamp(pause, 1, kPauseMax) : 1;
  record.left = fields >= 3 ? std::clamp(left, 0, kPauseMax) : 0;
  return record;
}

bool write_unconfirmed(const Unconfirmed &record) {
  const int fd = open(kUnconfirmed, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;
  char text[48];
  const int length = std::snprintf(text, sizeof(text), "%d %d %d\n",
                                   record.count, record.pause, record.left);
  const bool ok = write(fd, text, length) == length;
  close(fd);
  return ok;
}

/* The JS side reports through the executor's own logging hook. */
void console_log_hook(MonoString *message, int level, void *a, void *b) {
  const std::string text = text_of(reinterpret_cast<MonoObject *>(message));
  if (text.compare(0, 8, "ANIKETOS") == 0) {
    LOG_INFO("[HOMEJS] js: %s", text.substr(0, 400).c_str());
    if (text.compare(0, 14, "ANIKETOS alive") == 0) {
      unlink(kUnconfirmed); /* Home survived with the script in it */
      s_unconfirmed = {};
      AppStarts &starts = text.find("app=control-center") != std::string::npos
                              ? s_control_center_starts
                              : s_home_starts;
      starts = {};
    }
    /* the top-bar icon the script added was pressed */
    if (text.compare(0, 13, "ANIKETOS:menu") == 0)
      HomeMenu::Request();
    /* a choice in the Trophies app's options menu */
    if (text.compare(0, 16, "ANIKETOS:trophy|") == 0)
      TrophyTools::HandleScriptMessage(text.substr(16));
  }
  if (s_console_log_original)
    s_console_log_original(message, level, a, b);
}

bool eval(MonoObject *executor, const char *source, const char *url) {
  MonoMethod *method = nullptr;
  void *iter = nullptr;
  while (MonoMethod *candidate =
             mono_class_get_methods(s_executor_class, &iter)) {
    if (std::strcmp(mono_method_get_name(candidate), "EvalScript") != 0)
      continue;
    MonoMethodSignature *signature = mono_method_signature(candidate);
    if (signature && mono_signature_get_param_count(signature) == 2) {
      method = candidate;
      break;
    }
  }
  if (!method) {
    LOG_WARN("[HOMEJS] EvalScript(string, string) not found");
    return false;
  }
  void *args[2] = {mono_string_new(mono_domain_get(), source),
                   mono_string_new(mono_domain_get(), url)};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, executor, args, &exception);
  if (exception) {
    std::string message = describe(exception).substr(0, 1500);
    for (char &c : message)
      if (c == '\n' || c == '\r')
        c = ' ';
    for (size_t off = 0; off < message.size(); off += 400)
      LOG_WARN("[HOMEJS] eval failed: %s", message.substr(off, 400).c_str());
    return false;
  }
  return true;
}

const char kInjectedScript[] =
#include "home_inject_js.inc"
    ;

const char *presence_name() {
  switch ((AniketosTheme::CustomizationOn() ? g_settings.home_presence_dot : 0)) {
  case 1: return "online";
  case 2: return "busy";
  case 3: return "appearOffline";
  default: return "";
  }
}

void run_script_hook(MonoObject *executor, MonoObject *bundle) {
  MonoMethod *name_getter =
      mono_class_get_method_from_name(s_executor_class, "get_Name", 0);
  MonoObject *exception = nullptr;
  MonoObject *name = name_getter ? mono_runtime_invoke(name_getter, executor,
                                                       nullptr, &exception)
                                 : nullptr;
  const std::string app = exception ? std::string() : text_of(name);
  bool injected = false;
  LOG_INFO("[HOMEJS] RunScript in '%s'", app.c_str());
  const bool home = app.find("rnps-home") != std::string::npos;
  /* Home, when the new tab's tile row can be filled; Control Center for the
     presence dot and the custom clock. */
  const bool control_center =
      app.find("rnps-control-center") != std::string::npos &&
      ((AniketosTheme::CustomizationOn() ? g_settings.home_presence_dot : 0) !=
           0 ||
       ShellClock::CustomClockOn());
  if (home) {
    shellui_note_home_start();
    note_app_start(s_home_starts, "Home");
  } else if (control_center) {
    note_app_start(s_control_center_starts, "Control Center");
  }
  /* the Emulators tab, unless switched off, and the bookmark tab while it
     holds a game */
  const bool emulators = home && g_settings.home_tab_emulators;
  const bool bookmark = home && HomeBookmark::Shown();
  if (!s_disabled && !exists(kOff) &&
      ((home && HomeTabs::Ready()) || control_center)) {
    /* this start counted, and the pause to follow should it fail too */
    Unconfirmed next = s_unconfirmed;
    ++next.count;
    if (next.count >= kUnconfirmedStartsAllowed) {
      next.left = next.pause;
      next.pause = std::min(next.pause * 2, kPauseMax);
    }
    if (!write_unconfirmed(next))
      LOG_WARN("[HOMEJS] could not record the start count");
    const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
      close(fd);
    const std::string caption = HomeTabs::Caption(
        HomeTabs::kEmulatorsGlyph, toolbox_i18n::tr("home_tabs.emulators"));
    /* Games and Media as the bar shows them, for React to lay it out by */
    std::string games_name, games_glyph, media_name, media_glyph;
    HomeTabs::ScriptCaption(HomeTabs::StockTab::Games, &games_name,
                            &games_glyph);
    HomeTabs::ScriptCaption(HomeTabs::StockTab::Media, &media_name,
                            &media_glyph);
    /* the bookmark has no identity but the user's name: shown as typed, with
       no glyph of ours (one the user typed shows as typed) */
    const std::string bookmark_name = HomeBookmark::Name();
    const std::string config =
        std::string("globalThis.__aniketosConfig = {app: ") +
        (home ? "'home'" : "'control-center'") + ", space: " +
        (home ? "'emulators'" : "''") +
        ", emulators: " + (emulators ? "true" : "false") + ", caption: " +
        JsString(caption) + ", label: " +
        JsString(toolbox_i18n::tr("home_tabs.emulators")) + ", limit: " +
        std::to_string(HomeTabs::kEmulatorsContentLimit) +
        /* the top-bar icon, for a Home the byte patch has no profile for;
           taken back below once that is known */
        ", icon: " + JsString(home ? AniketosIconPath() : "") +
        /* the presence dot to show on avatars while not online */
        ", presence: " + JsString(presence_name()) +
        /* the custom clock's settings, drawn by the script in Sony's own
           clock (asked for again while it is up: aniketos.clock) */
        ", clock: " + ShellClock::ScriptConfig() +
        /* the bookmark tab: its caption (empty for none) and its name */
        ", bookmark: " + JsString(bookmark ? bookmark_name : std::string()) +
        ", bookmarkLabel: " + JsString(bookmark_name) +
        ", bookmarkLimit: " + std::to_string(HomeBookmark::kContentLimit) +
        ", bookmarkWatch: " + (home && HomeBookmark::Enabled() ? "true" : "false") +
        ", stockCaptions: {game: [" + JsString(games_name) + ", " +
        JsString(games_glyph) + "], media: [" + JsString(media_name) + ", " +
        JsString(media_glyph) + "]}" +
        "};";
    const bool ok = eval(executor, config.c_str(), "aniketos-config.js") &&
                    eval(executor, kInjectedScript, "aniketos-home.js");
    unlink(kPending);
    if (!ok)
      unlink(kUnconfirmed); /* nothing went in, so nothing to confirm */
    LOG_INFO("[HOMEJS] injected into '%s' ok=%d", app.c_str(), ok ? 1 : 0);
    injected = ok;
    if (ok)
      (home ? s_home_starts : s_control_center_starts).unconfirmed = true;
  }
  /* Before the bundle runs and asks for its rows: the emulators, and the
     games moved to the bookmark, leave the Games row only for a tab of
     their own. */
  if (home) {
    HomeTabs::SetEmulatorsTabShown(injected && emulators);
    HomeBookmark::SetHome(injected, injected && bookmark);
  }
  /* The Trophies app: View, Unlock and Lock in its options menus. A failure
     there costs that app alone, so only the in-flight marker guards it. */
  if (!s_disabled && !exists(kOff) && g_settings.onionhen_game_opts &&
      app.find("rnps-trophy") != std::string::npos) {
    const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
      close(fd);
    const std::string config = TrophyTools::ScriptConfig();
    const bool ok =
        eval(executor, config.c_str(), "aniketos-trophy-config.js") &&
        eval(executor, TrophyTools::InjectedScript(), "aniketos-trophy.js");
    unlink(kPending);
    LOG_INFO("[HOMEJS] injected into '%s' ok=%d", app.c_str(), ok ? 1 : 0);
  }
  /* The Information screen of a game or app: AniketosHEN's rows under the
     system's. Guarded like the Trophies app. */
  if (!s_disabled && !exists(kOff) && g_settings.onionhen_game_opts &&
      app.find("rnps-content-information") != std::string::npos) {
    const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
      close(fd);
    const std::string config = ContentInfo::ScriptConfig();
    const bool ok =
        eval(executor, config.c_str(), "aniketos-info-config.js") &&
        eval(executor, ContentInfo::InjectedScript(), "aniketos-info.js");
    unlink(kPending);
    LOG_INFO("[HOMEJS] injected into '%s' ok=%d", app.c_str(), ok ? 1 : 0);
  }
  if (s_run_script_original)
    s_run_script_original(executor, bundle);
  /* The bundle is patched while the original reads it, so only now is it
     known whether the byte patch put the icon in the bar itself. */
  if (injected && homeui_top_nav_patched()) {
    const bool cleared = eval(executor, "globalThis.__aniketosConfig.icon = '';",
                              "aniketos-icon.js");
    LOG_INFO("[HOMEJS] top-bar icon left to the byte patch ok=%d",
             cleared ? 1 : 0);
  }
}

} // namespace

using FlushQueue = void (*)(MonoString *, void *, void *);
FlushQueue s_flush_original = nullptr;
void flush_hook(MonoString *queue, void *a, void *b) {
  const std::string text = text_of(reinterpret_cast<MonoObject *>(queue));
  if (text.find("ANIKETOS-FLUSH") != std::string::npos) {
    LOG_INFO("[HOMEJS] js(flush): %s", text.substr(0, 400).c_str());
    return; /* ours: not a queue of native calls */
  }
  if (s_flush_original)
    s_flush_original(queue, a, b);
}

/* A reload asked for by the HEN (a setting changed, the radius slider)
   restarts the app before the script can report it running; that start is
   not a fall-over, so it does not count towards kRestartsAllowed. */
void NoteRequestedReload(const char *title_id) {
  if (!title_id)
    return;
  if (std::strcmp(title_id, "NPXS40002") == 0)
    s_home_starts.unconfirmed = false;
  else if (std::strcmp(title_id, "NPXS40003") == 0)
    s_control_center_starts.unconfirmed = false;
}

/* A JavaScript string literal in plain ASCII. The executor sizes the source
   by characters, not bytes, so anything outside ASCII (the glyph, most
   translations) has to travel as \uXXXX escapes or the script arrives cut
   short. */
std::string JsString(const std::string &text) {
  std::string out = "\"";
  const auto escape = [&out](unsigned unit) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "\\u%04x", unit & 0xffffu);
    out += buffer;
  };
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    unsigned code = lead;
    size_t length = 1;
    if (lead >= 0xf0 && i + 3 < text.size()) {
      code = lead & 0x07;
      length = 4;
    } else if (lead >= 0xe0 && i + 2 < text.size()) {
      code = lead & 0x0f;
      length = 3;
    } else if (lead >= 0xc0 && i + 1 < text.size()) {
      code = lead & 0x1f;
      length = 2;
    }
    for (size_t k = 1; k < length; ++k)
      code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
    i += length;
    if (code >= 0x10000) {
      code -= 0x10000;
      escape(0xd800 + (code >> 10));
      escape(0xdc00 + (code & 0x3ff));
    } else if (code < 0x20 || code > 0x7e || code == '"' || code == '\\') {
      escape(code);
    } else {
      out += static_cast<char>(code);
    }
  }
  return out + "\"";
}

void Install(MonoImage *react_pui) {
  if (!react_pui || s_run_script_original)
    return;
  /* Died inside an injection, or unconfirmed twice running: the script
     is left out for a pause of starts, then tried again (see kUnconfirmed). */
  s_unconfirmed = read_unconfirmed();
  if (exists(kPending)) {
    /* ShellUI died with the injection in flight: a failed start as well */
    unlink(kPending);
    if (s_unconfirmed.count < 1)
      s_unconfirmed.count = 1;
  }
  if (s_unconfirmed.count >= kUnconfirmedStartsAllowed &&
      s_unconfirmed.left > 0) {
    s_disabled = true;
    --s_unconfirmed.left;
    write_unconfirmed(s_unconfirmed);
    LOG_WARN("[HOMEJS] %d starts in a row never confirmed the script; left "
             "out for this start (%d more), then tried again (delete %s to "
             "try at once)",
             s_unconfirmed.count, s_unconfirmed.left, kUnconfirmed);
  } else if (s_unconfirmed.count > 0) {
    LOG_WARN("[HOMEJS] the last %d start(s) never confirmed the script; "
             "trying again%s",
             s_unconfirmed.count,
             s_unconfirmed.count >= kUnconfirmedStartsAllowed
                 ? " after the pause"
                 : "");
  }
  s_executor_class = mono_class_from_name(
      react_pui, "ReactNative.JavaScriptCore.Executor", "JSJavaScriptExecutor");
  const uint64_t run =
      s_executor_class
          ? Get_Address_of_Method(react_pui, s_executor_class, "RunScript", 1)
          : 0;
  const uint64_t log =
      s_executor_class
          ? Get_Address_of_Method(react_pui, s_executor_class, "ConsoleLog", 4)
          : 0;
  const bool run_hooked =
      run && InstallDetour(run, reinterpret_cast<void *>(&run_script_hook),
                           reinterpret_cast<void **>(&s_run_script_original));
  const bool log_hooked =
      log && InstallDetour(log, reinterpret_cast<void *>(&console_log_hook),
                           reinterpret_cast<void **>(&s_console_log_original));
  const uint64_t flush =
      s_executor_class
          ? Get_Address_of_Method(react_pui, s_executor_class,
                                  "NativeFlushQueueImmediate", 3)
          : 0;
  const bool flush_hooked =
      flush && InstallDetour(flush, reinterpret_cast<void *>(&flush_hook),
                             reinterpret_cast<void **>(&s_flush_original));
  LOG_INFO("[HOMEJS] hooks RunScript=%d ConsoleLog=%d Flush=%d",
           run_hooked ? 1 : 0, log_hooked ? 1 : 0, flush_hooked ? 1 : 0);
}

} // namespace HomeJs
