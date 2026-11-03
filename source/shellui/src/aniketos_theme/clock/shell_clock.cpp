#include "aniketos_theme/clock/shell_clock.hpp"

#include "aniketos_shell/core/host_scene.hpp"
#include "aniketos_shell/home/home_tabs.hpp"
#include "aniketos_theme/tiles/space_switcher_labels.hpp"
#include "clock_internal.hpp"

#include <onion/detour.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace ShellClock {

std::string s_last_rendered;
time_t s_last_second = static_cast<time_t>(-1);
bool s_force_refresh = false;
std::atomic<bool> s_game_focused{false};
void (*s_native_label_set_text_orig)(MonoObject *, MonoString *) = nullptr;

namespace {

uint32_t s_clock_handle = 0;
uint64_t s_settings_signature = 0;
uint64_t s_last_animation_frame_ms = 0;
/* A few frames of forced redraw after React has touched the label. */
int s_redraw_frames = 0;
MonoImage *s_pui_image = nullptr;
void (*s_native_label_on_update_orig)(MonoObject *) = nullptr;
/* Root widgets of the scenes a clock has been seen in, and the clock label of
   each. They outlive the label, which React replaces when it rebuilds the bar. */
constexpr size_t kKnownScenes = 2;
uint32_t s_known_roots[kKnownScenes] = {};
uint32_t s_known_labels[kKnownScenes] = {};

void bind_native_clock(MonoObject *label, const std::string &stock) {
  if (!label || !mono_gchandle_new_weakref || !mono_gchandle_get_target)
    return;
  if (has_clock_test_id(label) && find_clock_alias(label) < 0)
    remember_clock_alias(label, stock);
  /* Home's clock is never given up for another scene's (Settings and the
     Control Center have their own): everything hung in Home follows the
     clock we follow. The other one stays a second, dressed clock. */
  {
    MonoObject *followed = clock_target();
    if (followed && followed != label && !HomeScene::IsDisposed(followed) &&
        HomeScene::Contains(followed) && !HomeScene::Contains(label))
      return;
  }
  /* The size check tells Sony's clock from other labels that happen to show a
     time. A label we already know has been resized by us (a large font makes
     it taller than any stock clock), so it must not be asked again or it could
     never become the active clock a second time. */
  if (find_clock_alias(label) < 0 && !plausible_native_clock_geometry(label))
    return;
  const int alias_index = remember_clock_alias(label, stock);
  if (alias_index < 0)
    return;
  NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(alias_index)];
  if (clock_target() != label) {
    // A scene transition may create a second clock while the previous one is
    // still visible underneath it. Drop only the active reference; restoring
    // that old label to white causes the exact color flash seen in Control
    // Center. Its weak alias keeps the original geometry for its next update.
    release_clock();
    s_clock_handle = mono_gchandle_new_weakref(label, 0);
    if (!s_clock_handle)
      return;
    load_clock_alias(alias);
  } else {
    s_native.stock_time = stock;
  }
  s_native.visible_width = s_native.base_width;
  s_force_refresh = true;
  alias.stock_time = stock;
}

void native_label_set_text_hook(MonoObject *label, MonoString *text) {
  const std::string stock = label ? Mono_to_String(text) : std::string();
  const bool clock_like = looks_like_stock_clock(label, stock.c_str());
  if (!clock_like)
    let_go_of_label(label, stock);
  /* Sony's clock label is recognised by its test id where it has one; the
     size check is only for builds whose clock carries none. A label already
     resized for a large font fails that check, and used to be left showing
     the stock time for good. */
  if (clock_like && clock_target() != label &&
      (has_clock_test_id(label) || plausible_native_clock_geometry(label)))
    bind_native_clock(label, stock);
  else if (clock_like && clock_target() == label)
    s_native.stock_time = stock;
  else if (clock_like && find_clock_alias(label) < 0)
    note_unbound_clock(label, stock);

  if (clock_target() != label) {
    MonoString *display_text =
        AniketosTheme::SpaceSwitcherLabels::Decorate(label, text);
    if (display_text != text)
      prepare_space_tab_label(label, stock);
    write_label_text(label, display_text ? display_text : text);
    return;
  }
  const time_t now = std::time(nullptr);
  if (!custom_clock_drawn_here()) {
    /* Drawn by React: the other clocks stay as they are. */
    if (!drawn_by_script(label))
      restore_all_native_labels();
    s_date_overlay = {};
    s_last_rendered = stock;
    write_label_text(label, text);
    return;
  }
  std::tm local{};
  const std::string rendered =
      local_time(now, local) ? format_time(local) : std::string();
  if (rendered.empty() || !write_label_text(label, rendered)) {
    write_label_text(label, text);
    return;
  }
  dress_native_clock(label, rendered);
  publish_date_overlay(now, true);
  s_last_rendered = rendered;
  s_last_second = now;
  /* React sets the text first and the rest of the label's style after it
     (colour, size, position), which undoes the layout just applied. That
     happens once a minute, and until the next second's redraw the clock
     stood there in Sony's colour and box. Redraw on the frames that follow
     instead. */
  s_redraw_frames = 4;
}

/* Whether the update hook can act on a label that is no clock of ours,
   judged from its UTF-16 text without copying it: a clock needs a digit
   followed by the separator (ClockText::Parse, at most 48 bytes), a dressed
   tab caption our glyph first (UpdateFocusBounds), a stock one no digit, at
   most 24 bytes and the icons on (IsTabLabel). A label whose text was
   already found to be no caption is skipped until that text changes:
   hundreds of labels go through here while Home is being built. */
bool worth_reading(MonoObject *label, const MonoString *text) {
  const int32_t length = text ? text->length : 0;
  if (length <= 0 || length > 48)
    return false;
  const auto *chars = text->chars;
  if (chars[0] >= 0xE000 && chars[0] <= 0xF8FF)
    return true;
  bool digit = false;
  for (int32_t i = 0; i < length; ++i) {
    if (chars[i] < '0' || chars[i] > '9')
      continue;
    if (i + 1 < length && (chars[i + 1] == ':' || chars[i + 1] == '.'))
      return true;
    digit = true;
  }
  if (digit || length > 24 || !HomeTabs::IconsShown())
    return false;
  uint64_t hash = 1469598103934665603ull;
  for (int32_t i = 0; i < length; ++i)
    hash = (hash ^ chars[i]) * 1099511628211ull;
  struct Seen {
    MonoObject *label;
    uint64_t hash;
  };
  static Seen seen[64] = {};
  Seen &entry = seen[(reinterpret_cast<uintptr_t>(label) >> 4) % 64];
  if (entry.label == label && entry.hash == hash)
    return false;
  if (AniketosTheme::SpaceSwitcherLabels::IsTabLabel(
          Mono_to_String(const_cast<MonoString *>(text))))
    return true;
  entry = {label, hash};
  return false;
}

void native_label_on_update_hook(MonoObject *label) {
  if (s_native_label_on_update_orig)
    s_native_label_on_update_orig(label);
  if (!label)
    return;
  int alias_index = find_clock_alias(label);
  if (clock_target() == label || alias_index >= 0) {
    if (HomeScene::IsDisposed(label)) {
      if (clock_target() == label)
        release_clock();
      forget_clock_alias(label);
      return;
    }
    const time_t now = std::time(nullptr);
    if (drawn_by_script(label))
      return; /* React draws this one */
    if (!custom_clock_on()) {
      restore_all_native_labels();
      s_date_overlay = {};
      return;
    }
    if (alias_index < 0) {
      alias_index = remember_clock_alias(label, raw_text(label));
      if (alias_index < 0)
        return;
    }
    const bool active = clock_target() == label;
    const float opacity = native_clock_opacity(label);
    if (!active && opacity > kShownOpacity && !s_native.visible) {
      const std::string stock =
          s_clock_aliases[static_cast<size_t>(alias_index)].stock_time;
      bind_native_clock(label, stock);
      alias_index = find_clock_alias(label);
      if (alias_index < 0)
        return;
    }
    update_clock_alias(label, static_cast<size_t>(alias_index), now);
    return;
  }
  MonoString *text = label_text(label);
  if (!worth_reading(label, text))
    return;
  const std::string stock = Mono_to_String(text);
  AniketosTheme::SpaceSwitcherLabels::UpdateFocusBounds(label, stock);
  // HomeUI may create the native tab labels before our setter hook is
  // installed. Recheck their live text during the runtime-resolved Label
  // update hook, then apply the same localized, semantic decorator once.
  // After decoration the exact-label check is false, so this does not allocate
  // or rewrite the label on every frame.
  if (stock.size() <= 24 &&
      AniketosTheme::SpaceSwitcherLabels::IsTabLabel(stock)) {
    if (MonoString *current_text = ui_string(stock)) {
      MonoString *display_text =
          AniketosTheme::SpaceSwitcherLabels::Decorate(label, current_text);
      if (display_text && display_text != current_text) {
        prepare_space_tab_label(label, stock);
        write_label_text(label, display_text);
      }
    }
    return;
  }
  if (looks_like_stock_clock(label, stock.c_str()) &&
      plausible_native_clock_geometry(label))
    bind_native_clock(label, stock);
}

/* May `label` replace `followed` as the clock we follow? Home's clock is
   never given up for another scene's, and is always preferred over one;
   otherwise the one on screen wins. */
bool should_follow(MonoObject *label, MonoObject *followed) {
  const bool followed_alive = followed && !HomeScene::IsDisposed(followed);
  const bool label_home = HomeScene::Contains(label);
  const bool followed_home = followed_alive && HomeScene::Contains(followed);
  if (label_home != followed_home)
    return label_home;
  const bool followed_shown =
      followed_alive && native_clock_opacity(followed) > kShownOpacity;
  return !followed_shown && native_clock_opacity(label) > kShownOpacity;
}

/* Label.set_SpannableText: the setter React really uses for the clock. At
   the turn of every minute it hands the label Sony's "HH:MM" this way, and
   the label draws it (white, no seconds) even though its plain Text still
   reads as ours, which is why the clock fell back to the stock look for the
   whole first second of each minute. For the clock label the request is
   answered with our own time instead; for any other label it goes through
   untouched. */
using SetSpannable = void (*)(MonoObject *, MonoObject *);
SetSpannable s_set_spannable_original = nullptr;

void set_spannable_hook(MonoObject *label, MonoObject *spannable) {
  std::string stock;
  if (label && spannable && mono_object_to_string) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(spannable, &exception);
    if (!exception)
      stock = Mono_to_String(shown);
  }
  const bool clock_like = looks_like_stock_clock(label, stock.c_str());
  if (!clock_like)
    let_go_of_label(label, stock);
  if (clock_like && clock_target() != label &&
      (has_clock_test_id(label) || plausible_native_clock_geometry(label))) {
    /* Another clock (the Control Center has its own). It becomes the one we
       follow if it is the one on screen; otherwise it is only remembered, so
       it is dressed the moment it shows. */
    if (should_follow(label, clock_target())) {
      bind_native_clock(label, stock);
    } else {
      /* The clock we follow is on screen and stays the one we follow. This
         one is dressed as a second clock, here and now. */
      const int alias = remember_clock_alias(label, stock);
      if (alias >= 0 && custom_clock_drawn_for(label) &&
          s_native_label_set_text_orig &&
          update_clock_alias(label, static_cast<size_t>(alias),
                             std::time(nullptr)))
        return;
    }
  } else if (clock_like && clock_target() != label &&
             find_clock_alias(label) < 0) {
    note_unbound_clock(label, stock);
  }
  if (clock_target() == label && custom_clock_drawn_here() &&
      s_native_label_set_text_orig) {
    if (clock_like)
      s_native.stock_time = stock;
    const time_t now = std::time(nullptr);
    std::tm local{};
    if (local_time(now, local)) {
      const std::string rendered = format_time(local);
      if (write_label_text(label, rendered)) {
        layout_native_clock(label, rendered);
        s_last_rendered = rendered;
        s_last_second = now;
        s_redraw_frames = 4;
        static int logged = 0;
        if (logged < 6) {
          ++logged;
          LOG_INFO("[CLOCK] React asked for '%s'; drawn as '%s'",
                   stock.c_str(), rendered.c_str());
        }
        return;
      }
    }
  }
  if (s_set_spannable_original)
    s_set_spannable_original(label, spannable);
}

MonoObject *search_clock_label(MonoObject *widget, int depth, int &budget) {
  if (!widget || depth > 14 || budget-- <= 0 || HomeScene::IsDisposed(widget))
    return nullptr;
  if (has_clock_test_id(widget))
    return widget;
  MonoObject *child = HomeScene::PropertyObject(widget, "FirstChild");
  for (int guard = 0; child && guard < 48; ++guard) {
    if (MonoObject *found = search_clock_label(child, depth + 1, budget))
      return found;
    child = HomeScene::PropertyObject(child, "NextSibling");
  }
  return nullptr;
}

MonoObject *find_clock_label(MonoObject *root) {
  int budget = 600;
  return search_clock_label(root, 0, budget);
}

/* No clock is being followed. The hooks pick a new label up when its text
   is set; if that moment was missed (the label was made before the hook
   could see it, or failed the checks), look for it by its test id in the
   scenes where the clock used to be. */
void recover_clock() {
  for (uint32_t handle : s_known_roots) {
    MonoObject *root = Reflect::Target(handle);
    if (!root || HomeScene::IsDisposed(root))
      continue;
    MonoObject *label = find_clock_label(root);
    if (!label || native_clock_opacity(label) <= kShownOpacity)
      continue;
    const std::string stock = raw_text(label);
    bind_native_clock(label, stock);
    LOG_INFO("[CLOCK] clock label found again by its test id (%p '%s')",
             static_cast<void *>(label), stock.c_str());
    return;
  }
}

/* Once a second every clock label in the scenes we know is looked at, not
   only the one being followed. One that is on screen showing Sony's own
   time, while the custom clock is enabled, is taken over there and then, and
   the circumstances are logged: whichever route let a stock clock through
   (React rebuilding the bar, a label the text hook judged not to be the
   clock, two clocks on screen at once), this is where it ends. */
void sweep_clock_labels(time_t now) {
  if (!custom_clock_on())
    return;
  static int rescan = 0;
  const bool may_rescan = --rescan <= 0;
  if (may_rescan)
    rescan = 3;
  std::tm local{};
  if (!local_time(now, local))
    return;
  /* The clock followed should be Home's whenever Home has one. */
  if (may_rescan) {
    MonoObject *followed = clock_target();
    MonoObject *home = HomeScene::OwnRoot();
    if (home && !(followed && !HomeScene::IsDisposed(followed) &&
                  HomeScene::Contains(followed))) {
      MonoObject *label = find_clock_label(home);
      if (label && label != followed) {
        bind_native_clock(label, raw_text(label));
        LOG_INFO("[CLOCK] following Home's clock again (%p)",
                 static_cast<void *>(label));
      }
    }
  }
  for (size_t i = 0; i < kKnownScenes; ++i) {
    MonoObject *root = Reflect::Target(s_known_roots[i]);
    if (!root || HomeScene::IsDisposed(root))
      continue;
    MonoObject *label = Reflect::Target(s_known_labels[i]);
    if ((!label || HomeScene::IsDisposed(label)) && may_rescan) {
      label = find_clock_label(root);
      Reflect::Hold(s_known_labels[i], label);
    }
    if (!label || HomeScene::IsDisposed(label) ||
        native_clock_opacity(label) <= kShownOpacity ||
        drawn_by_script(label))
      continue;
    const std::string shown = raw_text(label);
    if (!looks_like_stock_clock(label, shown.c_str()) || shown == format_time(local))
      continue;
    MonoObject *target = clock_target();
    LOG_INFO("[CLOCK] stock time '%s' on screen: label=%p target=%p "
             "known=%d target_opacity=%.2f; taking it over",
             shown.c_str(), static_cast<void *>(label),
             static_cast<void *>(target), find_clock_alias(label) >= 0 ? 1 : 0,
             target ? native_clock_opacity(target) : -1.0f);
    if (target == label) {
      s_force_refresh = true; /* ours already: the next tick redraws it */
      s_last_rendered.clear();
    } else if (should_follow(label, target)) {
      bind_native_clock(label, shown);
    } else {
      /* Another clock is being followed and is on screen too: dress this
         one as well, as a second clock. */
      const int alias = remember_clock_alias(label, shown);
      if (alias >= 0)
        update_clock_alias(label, static_cast<size_t>(alias), now);
    }
  }
}

/* Text the same way as restyled_by_react, thirty times a second: at the turn of
   the minute React writes Sony's time, and not always by the setter the hook
   is on. */
void check_shown_text(MonoObject *target) {
  if (!s_native.customized || !custom_clock_drawn_here() ||
      s_last_rendered.empty() || s_native.opacity <= kShownOpacity)
    return;
  const std::string shown = raw_text(target);
  if (shown == s_last_rendered)
    return;
  static int logged = 0;
  if (logged < 20) {
    ++logged;
    LOG_INFO("[CLOCK] the clock showed '%s' instead of '%s'; redrawn",
             shown.c_str(), s_last_rendered.c_str());
  }
  s_last_rendered.clear();
  s_redraw_frames = std::max(s_redraw_frames, 2);
}

/* The clock we follow is not on screen. If another known clock is (Home
   again after the Control Center closed, for instance), follow that one.
   This used to depend on that label's own update callback, which PUI only
   runs when the label changes. */
bool follow_shown_alias(MonoObject *target) {
  for (NativeClockAlias &alias : s_clock_aliases) {
    MonoObject *other = Reflect::Target(alias.handle);
    if (!other || other == target || HomeScene::IsDisposed(other) ||
        native_clock_opacity(other) <= kShownOpacity)
      continue;
    const std::string stock = alias.stock_time;
    bind_native_clock(other, stock);
    return true;
  }
  return false;
}

/* Diagnostic: every readable property of an object, one line per 400 bytes.
   Only reached from the profiler script. */
void dump_object(MonoObject *node, const char *tag) {
  if (!node)
    return;
  std::string line;
  for (MonoClass *klass = mono_object_get_class(node); klass;
       klass = mono_class_get_parent(klass)) {
    const char *class_name = mono_class_get_name(klass);
    if (!class_name || !std::strcmp(class_name, "Object"))
      break;
    line += "#";
    line += class_name;
    line += ";";
    void *iter = nullptr;
    while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
      const char *name = mono_method_get_name(method);
      if (!name || std::strncmp(name, "get_", 4) != 0)
        continue;
      uint32_t iflags = 0;
      if (mono_method_get_flags(method, &iflags) & 0x0010) /* static */
        continue;
      MonoMethodSignature *sig = mono_method_signature(method);
      if (!sig || mono_signature_get_param_count(sig) != 0)
        continue;
      MonoObject *exception = nullptr;
      MonoObject *value = mono_runtime_invoke(method, node, nullptr, &exception);
      std::string text = "null";
      if (exception)
        text = "!exc";
      else if (value) {
        MonoObject *exc2 = nullptr;
        MonoString *str = mono_object_to_string(value, &exc2);
        text = exc2 || !str ? "?" : Mono_to_String(str);
      }
      if (text.size() > 120)
        text.resize(120);
      line += name + 4;
      line += '=';
      line += text;
      line += ';';
    }
  }
  for (size_t off = 0; off < line.size(); off += 400)
    LOG_INFO("[CLOCKDUMP] %s %p %s", tag ? tag : "",
             static_cast<void *>(node), line.substr(off, 400).c_str());
}

} // namespace

MonoObject *clock_target() { return Reflect::Target(s_clock_handle); }

void release_clock() {
  Reflect::Release(s_clock_handle);
  s_last_rendered.clear();
  s_last_second = static_cast<time_t>(-1);
  s_animation_started_ms = 0;
  s_last_animation_frame_ms = 0;
  std::string stock_time = std::move(s_native.stock_time);
  s_native = {};
  s_native.stock_time = std::move(stock_time);
  s_date_overlay = {};
}

/* Diagnostic: write the loaded PUI assembly out so it can be decompiled. */
void DebugDumpPuiImage(const char *path) {
  if (!s_pui_image || !path)
    return;
  struct ImageHead {
    int ref_count;
    void *raw_data_handle;
    const char *raw_data;
    uint32_t raw_data_len;
  };
  const ImageHead *head =
      reinterpret_cast<const ImageHead *>(s_pui_image);
  LOG_INFO("[CLOCKDUMP] pui image data=%p len=%u",
           static_cast<const void *>(head->raw_data), head->raw_data_len);
  if (reinterpret_cast<uintptr_t>(head->raw_data) < 0x100000000ull ||
      head->raw_data_len < 0x1000 || head->raw_data_len > 64u * 1024 * 1024)
    return;
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    LOG_INFO("[CLOCKDUMP] pui image open errno=%d", errno);
    return;
  }
  size_t off = 0;
  while (off < head->raw_data_len) {
    const ssize_t n = write(fd, head->raw_data + off,
                            std::min<size_t>(head->raw_data_len - off, 1 << 20));
    if (n <= 0)
      break;
    off += static_cast<size_t>(n);
  }
  close(fd);
  LOG_INFO("[CLOCKDUMP] pui image wrote=%zu", off);
}

void Install(MonoImage *pui) {
  s_pui_image = pui;
  if (!pui || !mono_class_from_name) {
    LOG_WARN("[HOME-SPACES:NATIVE] UI3.Label.Text hook skipped pui=%p "
             "class_resolver=%d",
             static_cast<void *>(pui), mono_class_from_name ? 1 : 0);
    return;
  }
  MonoClass *native_label =
      mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Label");
  const uint64_t span =
      native_label
          ? Get_Address_of_Method(pui, native_label, "set_SpannableText", 1)
          : 0;
  const bool hooked =
      span && InstallDetour(span, reinterpret_cast<void *>(&set_spannable_hook),
                            reinterpret_cast<void **>(&s_set_spannable_original));
  LOG_INFO("[CLOCK] SpannableText hook target=0x%lx installed=%d",
           static_cast<unsigned long>(span), hooked ? 1 : 0);
  if (!hooked)
    s_set_spannable_original = nullptr;
  HomeScene::ResolveDisposedGetter(native_label);
  MonoProperty *native_text =
      native_label && mono_class_get_property_from_name
          ? mono_class_get_property_from_name(native_label, "Text")
          : nullptr;
  MonoMethod *native_text_setter =
      native_text && mono_property_get_set_method
          ? mono_property_get_set_method(native_text)
          : nullptr;
  const uint64_t native_text_address =
      native_text_setter && mono_compile_method
          ? mono_compile_method(native_text_setter)
          : 0;
  bool native_text_hook_installed = false;
  if (native_text_address)
    native_text_hook_installed = InstallDetour(
        native_text_address,
        reinterpret_cast<void *>(&native_label_set_text_hook),
        reinterpret_cast<void **>(&s_native_label_set_text_orig));
  LOG_INFO("[HOME-SPACES:NATIVE] UI3.Label.Text class=%p property=%p "
           "setter=%p target=0x%lx installed=%d original=%p",
           static_cast<void *>(native_label), static_cast<void *>(native_text),
           static_cast<void *>(native_text_setter),
           static_cast<unsigned long>(native_text_address),
           native_text_hook_installed ? 1 : 0,
           reinterpret_cast<void *>(s_native_label_set_text_orig));
  const uint64_t native_update_address =
      native_label
          ? Get_Address_of_Method(pui, native_label, "OnUpdate", 0)
          : 0;
  bool native_update_hook_installed = false;
  if (native_update_address)
    native_update_hook_installed = InstallDetour(
        native_update_address,
        reinterpret_cast<void *>(&native_label_on_update_hook),
        reinterpret_cast<void **>(&s_native_label_on_update_orig));
  LOG_INFO("[HOME-SPACES:NATIVE] UI3.Label.OnUpdate class=%p "
           "target=0x%lx installed=%d original=%p",
           static_cast<void *>(native_label),
           static_cast<unsigned long>(native_update_address),
           native_update_hook_installed ? 1 : 0,
           reinterpret_cast<void *>(s_native_label_on_update_orig));
}

void Tick() {
  const time_t now = std::time(nullptr);
  sync_clock_language(now);
  {
    static time_t swept = 0;
    if (now != swept) {
      swept = now;
      sweep_clock_labels(now);
      s_game_focused = AniketosShell::GameLayerFocused();
    }
  }
  MonoObject *target = clock_target();
  if (!target) {
    static int recover_countdown = 0;
    if (--recover_countdown <= 0) {
      recover_countdown = 60;
      recover_clock();
    }
    if (s_clock_handle)
      release_clock();
    s_date_overlay = {};
    return;
  }
  /* Whether Sony's clock label is alive and how opaque it is means walking
     its parent chain. Its fades last hundreds of milliseconds, so sampling
     at 30 Hz tracks them just as well as doing it on every frame. */
  static uint64_t s_last_probe_ms = 0;
  const uint64_t probe_ms = MonotonicMs();
  const bool probe = s_force_refresh || probe_ms - s_last_probe_ms >= 33;
  if (probe)
    s_last_probe_ms = probe_ms;
  if (probe && HomeScene::IsDisposed(target)) {
    release_clock();
    forget_clock_alias(target);
    return;
  }
  if (probe) {
    const int target_alias = find_clock_alias(target);
    if (target_alias >= 0)
      adopt_native_geometry(
          target, s_clock_aliases[static_cast<size_t>(target_alias)], true);
  }
  /* React put its own geometry back (it re-renders the bar now and then):
     redraw at once rather than at the next second. Two plain property reads
     a frame. */
  if (restyled_by_react(target))
    s_redraw_frames = std::max(s_redraw_frames, 2);
  if (probe)
    check_shown_text(target);
  if (s_redraw_frames > 0) {
    --s_redraw_frames;
    s_force_refresh = true;
  }
  const float opacity =
      probe ? native_clock_opacity(target) : s_native.opacity;
  if (probe && opacity <= kShownOpacity && follow_shown_alias(target))
    return;
  const bool opacity_changed =
      std::abs(opacity - s_native.opacity) > 0.001f;
  s_native.opacity = opacity;
  const bool visible = opacity > kShownOpacity;
  const bool visibility_changed = visible != s_native.visible;
  s_native.visible = visible;
  /* Leave a clock that is not on screen alone. PUI renders a label's text
     asynchronously and only for scenes that are being drawn; a text or size
     change made behind Settings or the Control Center left that render
     pending for good (IsReadyToRenderInternal stayed false), and the time was
     never painted again once Home came back. Everything is brought up to date
     the moment the clock is shown again. */
  if (!visible && custom_clock_drawn_here()) {
    if (visibility_changed)
      publish_date_overlay(now, true);
    s_force_refresh = true;
    return;
  }
  const uint64_t signature = settings_signature();
  const bool settings_changed = signature != s_settings_signature;
  const uint64_t frame_ms = MonotonicMs();
  const bool animation_frame =
      clock_animation_active() &&
      frame_ms - s_last_animation_frame_ms >= kAnimationFrameMs;
  if (!s_force_refresh && now == s_last_second && !settings_changed &&
      !visibility_changed && !opacity_changed && !animation_frame)
    return;
  /* An animation frame on its own only moves the colour. Text and geometry
     are refreshed once a second and whenever something actually changed;
     re-stating a dozen layout properties of Sony's clock label twenty times a
     second invalidated its layout for nothing. */
  const bool colour_only = animation_frame && !s_force_refresh &&
                           now == s_last_second && !settings_changed &&
                           !visibility_changed && !opacity_changed &&
                           custom_clock_drawn_here() &&
                           s_native.customized;
  s_force_refresh = false;
  s_settings_signature = signature;
  if (!clock_animation_active())
    s_animation_started_ms = 0;
  if (animation_frame)
    s_last_animation_frame_ms = frame_ms;

  if (!custom_clock_drawn_here()) {
    publish_date_overlay(now, false);
    /* Switched off: every clock gets Sony's look and time back. Drawn by
       React: that clock is React's, and any other is dressed by its hooks. */
    if (!custom_clock_on()) {
      restore_all_native_labels();
      if (s_last_rendered != s_native.stock_time || settings_changed) {
        write_label_text(target, s_native.stock_time);
      }
    }
    s_last_rendered = s_native.stock_time;
    s_last_second = now;
    return;
  }

  if (colour_only) {
    apply_custom_time_color(target);
    publish_date_overlay(now, true);
    return;
  }

  std::tm local{};
  if (!local_time(now, local))
    return;
  const std::string rendered = format_time(local);
  /* Checked against what the label really shows, not only against what was
     last written: React can put its own time back by a route the text hook
     does not see, and with seconds hidden nothing else would notice for up
     to a minute. */
  const bool overwritten = raw_text(target) != rendered;
  if (overwritten && rendered == s_last_rendered)
    LOG_INFO("[CLOCK] label text had been replaced; custom time restored");
  if (rendered != s_last_rendered || settings_changed || overwritten) {
    write_label_text(target, rendered);
    s_last_rendered = rendered;
  }
  dress_native_clock(target, rendered);
  publish_date_overlay(now, true);
  s_last_second = now;
}

void DebugSnapshot(char *out, size_t out_size) {
  if (!out || !out_size)
    return;
  /* Deliberately free of the time text, so the line only changes when the
     clock's state does. */
  MonoObject *target = clock_target();
  MonoClass *ready_class =
      target ? property_class(target, "IsReadyToRenderInternal") : nullptr;
  size_t used = static_cast<size_t>(std::snprintf(
      out, out_size,
      "target=%p disp=%d op=%.2f vis=%d cust=%d base(x=%.0f w=%.0f h=%.0f) "
      "rdy=%d label(x=%.0f y=%.0f w=%.0f h=%.0f len=%zu) date(vis=%d op=%.2f geo=%d)",
      static_cast<void *>(target), target ? (HomeScene::IsDisposed(target) ? 1 : 0) : -1,
      target ? native_clock_opacity(target) : -1.0f, s_native.visible ? 1 : 0,
      s_native.customized ? 1 : 0, s_native.base_x, s_native.base_width,
      s_native.base_height,
      ready_class ? (Get_Property<bool>(ready_class, target,
                                        "IsReadyToRenderInternal")
                         ? 1
                         : 0)
                  : -1,
      target ? label_dimension(target, "X") : -1.0f,
      target ? label_dimension(target, "Y") : -1.0f,
      target ? label_dimension(target, "Width") : -1.0f,
      target ? label_dimension(target, "Height") : -1.0f,
      target ? raw_text(target).size() : 0, s_date_overlay.visible ? 1 : 0,
      s_date_overlay.opacity, s_date_overlay.has_global_time_geometry ? 1 : 0));
  float mirror = -1.0f;
  GetNativeClockOpacity(&mirror);
  if (used < out_size)
    used += static_cast<size_t>(std::snprintf(out + used, out_size - used,
                                              " mirror=%.2f tw=%.1f/%.1f cx=%.1f "
                                              "text='%s'",
                                              mirror, s_measured_time_width,
                                              s_native.visible_width,
                                              s_native.center_x,
                                              s_date_overlay.text.c_str()));
  for (size_t i = 0; i < s_clock_aliases.size() && used < out_size; ++i) {
    NativeClockAlias &alias = s_clock_aliases[i];
    MonoObject *label = Reflect::Target(alias.handle);
    if (!label)
      continue;
    used += static_cast<size_t>(std::snprintf(
        out + used, out_size - used,
        " | a%zu=%p%s op=%.2f base(x=%.0f w=%.0f) label(x=%.0f w=%.0f len=%zu) "
        "cust=%d",
        i, static_cast<void *>(label), label == target ? "*" : "",
        native_clock_opacity(label), alias.base_x, alias.base_width,
        label_dimension(label, "X"), label_dimension(label, "Width"),
        raw_text(label).size(), alias.customized ? 1 : 0));
  }
}

void DebugDumpProperties(const char *tag) {
  (void)tag;
  dump_object(
      HomeScene::PropertyObject(HomeScene::TopWidget(clock_target()), "ParentScene"),
      "scene-steady");
}

MonoObject *ClockSceneRoot() {
  MonoObject *target = clock_target();
  if (!target || !mono_gchandle_new_weakref || HomeScene::IsDisposed(target))
    return nullptr;
  static uint32_t root_handle = 0;
  static uint32_t root_for_handle = 0;
  MonoObject *root_for = Reflect::Target(root_for_handle);
  MonoObject *root =
      root_for == target ? Reflect::Target(root_handle) : nullptr;
  if (!root) {
    Reflect::Release(root_handle);
    Reflect::Release(root_for_handle);
    root = HomeScene::TopWidget(target);
    if (root == target)
      return nullptr;
    root_handle = mono_gchandle_new_weakref(root, 0);
    /* Remembered for recover_clock(), two scenes at most (Home and the
       Control Center). */
    bool known = false;
    for (uint32_t handle : s_known_roots)
      known = known || Reflect::Target(handle) == root;
    if (!known) {
      static size_t next = 0;
      Reflect::Hold(s_known_roots[next], root);
      Reflect::Release(s_known_labels[next]);
      next = (next + 1) % kKnownScenes;
    }
    root_for_handle = mono_gchandle_new_weakref(target, 0);
  }
  return HomeScene::IsDisposed(root) ? nullptr : root;
}

bool GetDateOverlayState(DateOverlayState *state) {
  if (!state)
    return false;
  *state = s_date_overlay;
  return state->visible;
}

void RequestRefresh() { s_force_refresh = true; }

} // namespace ShellClock
