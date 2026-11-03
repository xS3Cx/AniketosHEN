#include "aniketos_theme/animation.hpp"
#include "aniketos_theme/clock/clock_text.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_theme/global_theme.hpp"
#include "aniketos_theme/theme_config.hpp"
#include "aniketos_theme/tiles/space_switcher_labels.hpp"
#include "clock_internal.hpp"

#include <onion/settings.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ShellClock {

NativeClockState s_native;
std::array<NativeClockAlias, kMaxNativeClockAliases> s_clock_aliases{};
DateOverlayState s_date_overlay{};
float s_measured_time_width = 0.0f;
uint64_t s_animation_started_ms = 0;

namespace {

uint32_t s_custom_time_color_handle = 0;
void *s_custom_time_color_value = nullptr;
int s_custom_time_color_r = -1;
int s_custom_time_color_g = -1;
int s_custom_time_color_b = -1;
/* The geometry last given to the followed label, to notice when React
   writes its own back. */
float s_applied_x = 0.0f;
float s_applied_width = 0.0f;

/* A clock our script draws in React itself (home_inject_js.inc, in Home and
   the Control Center) carries Sony's test id with this ending. Such a label
   is followed like any clock, for Home's root and for the opacity, but
   nothing here writes into it and the date overlay is not drawn for it; a
   clock without it (no script in that app, or switched off there) is
   dressed here as before. */
constexpr char kScriptClockMark[] = ".aniketos";

AniketosTheme::RGB effective_clock_color() {
  if (AniketosTheme::GlobalTheme::Active())
    return AniketosTheme::GlobalTheme::Color(MonotonicMs());
  if (!clock_animation_active()) {
    return {std::clamp(g_settings.shell_clock_time_r, 0, 255) / 255.0f,
            std::clamp(g_settings.shell_clock_time_g, 0, 255) / 255.0f,
            std::clamp(g_settings.shell_clock_time_b, 0, 255) / 255.0f};
  }
  const uint64_t now = MonotonicMs();
  if (!s_animation_started_ms)
    s_animation_started_ms = now;
  const AniketosTheme::ThemeConfig config =
      AniketosTheme::ConfigFromSettings(g_settings);
  const uint64_t elapsed_ms =
      ((now - s_animation_started_ms) / kAnimationFrameMs) *
      kAnimationFrameMs;
  return AniketosTheme::EvaluateAnimation(
             config, static_cast<float>(elapsed_ms) / 1000.0f)
      .glow;
}

MonoClass *resolve_property_class(MonoObject *object, const char *name) {
  if (!object || !name || !mono_object_get_class || !mono_class_get_parent ||
      !mono_class_get_property_from_name)
    return nullptr;
  for (MonoClass *klass = mono_object_get_class(object); klass;
       klass = mono_class_get_parent(klass)) {
    if (mono_class_get_property_from_name(klass, name))
      return klass;
  }
  return nullptr;
}

template <typename Value>
void set_object_property(MonoObject *object, const char *name, Value value) {
  if (MonoClass *klass = property_class(object, name))
    Set_Property(klass, object, name, value);
}

template <typename Value>
void set_object_property_invoke(MonoObject *object, const char *name,
                                Value value) {
  if (MonoClass *klass = property_class(object, name))
    Set_Property_Invoke(klass, object, name, value);
}

size_t utf8_glyph_count(const std::string &text) {
  size_t count = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xc0u) != 0x80u)
      ++count;
  }
  return count;
}

int color_byte(float channel) {
  return static_cast<int>(std::lround(channel * 255.0f));
}

int time_font_size() {
  return std::clamp(g_settings.shell_clock_time_font_size, 24, 56);
}

/* The label's own TextColor, boxed and pinned (0 if it cannot be read):
   what it gets back if it turns out not to be a clock. */
uint32_t pinned_text_color(MonoObject *label) {
  MonoClass *klass = property_class(label, "TextColor");
  MonoProperty *property = klass && mono_class_get_property_from_name
                               ? mono_class_get_property_from_name(klass,
                                                                   "TextColor")
                               : nullptr;
  MonoMethod *getter = property && mono_property_get_get_method
                           ? mono_property_get_get_method(property)
                           : nullptr;
  if (!getter || !mono_runtime_invoke || !mono_gchandle_new)
    return 0;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, label, nullptr, &exception);
  return boxed && !exception ? mono_gchandle_new(boxed, 1) : 0;
}

void clear_alias(NativeClockAlias &alias) {
  Reflect::Release(alias.handle);
  Reflect::Release(alias.stock_color);
  alias = {};
}

bool native_clock_visible(MonoObject *label) {
  if (!label)
    return false;

  // PUI's final visibility flags on a leaf can remain true while its scene
  // root is being swapped.  During a game launch/return that left the hidden
  // Home clock looking active, so the newly visible SystemOverlay/Home clock
  // was never selected.  Check the bounded native-parent chain as well as the
  // label itself; HomeScene::Parent() resolves the getter at runtime for firmware
  // portability and safely stops where a parent API is unavailable.
  MonoObject *node = label;
  for (int depth = 0; node && depth < 12; ++depth) {
    if (HomeScene::IsDisposed(node))
      return false;
    if (MonoClass *klass = property_class(node, "IsFinallyShown")) {
      if (!Get_Property<bool>(klass, node, "IsFinallyShown"))
        return false;
    }
    if (MonoClass *klass = property_class(node, "IsFinalOpacityVisible")) {
      if (!Get_Property<bool>(klass, node, "IsFinalOpacityVisible"))
        return false;
    }
    if (MonoClass *klass = property_class(node, "FinalOpacity")) {
      if (Get_Property<float>(klass, node, "FinalOpacity") <= 0.001f)
        return false;
    } else if (MonoClass *klass = property_class(node, "Opacity")) {
      if (Get_Property<float>(klass, node, "Opacity") <= 0.001f)
        return false;
    }
    MonoObject *parent = HomeScene::Parent(node);
    if (!parent || parent == node)
      break;
    node = parent;
  }
  return true;
}

// UI3 positions are relative to their parents, while the date is attached to
// the UI2 overlay root. Sum the native clock ancestry so both labels share a
// screen-space center. Ignore incomplete hierarchies during scene replacement.
bool global_label_geometry(MonoObject *label, float width, float height,
                           float &center_x, float &bottom_y) {
  if (!label || width <= 0.0f || height <= 0.0f)
    return false;
  float x = label_dimension(label, "X");
  float y = label_dimension(label, "Y");
  MonoObject *node = label;
  int parent_count = 0;
  for (; parent_count < 24; ++parent_count) {
    MonoObject *parent = HomeScene::Parent(node);
    if (!parent || parent == node)
      break;
    x += label_dimension(parent, "X");
    y += label_dimension(parent, "Y");
    node = parent;
  }
  if (parent_count == 0)
    return false;
  center_x = x + width * 0.5f;
  bottom_y = y + height;
  return center_x > 0.0f && center_x < 4096.0f && bottom_y > 0.0f &&
         bottom_y < 512.0f;
}

bool ensure_custom_time_color() {
  const AniketosTheme::RGB color = effective_clock_color();
  const int r = color_byte(color.r);
  const int g = color_byte(color.g);
  const int b = color_byte(color.b);
  if (s_custom_time_color_value && s_custom_time_color_handle &&
      s_custom_time_color_r == r && s_custom_time_color_g == g &&
      s_custom_time_color_b == b)
    return true;
  MonoObject *boxed_color =
      CreateUIColorBoxed(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
  if (!boxed_color || !mono_object_unbox || !mono_gchandle_new ||
      !mono_gchandle_get_target)
    return false;
  uint32_t color_handle = mono_gchandle_new(boxed_color, 1);
  MonoObject *pinned_box = Reflect::Target(color_handle);
  void *color_value = pinned_box ? mono_object_unbox(pinned_box) : nullptr;
  if (!color_value) {
    Reflect::Release(color_handle);
    return false;
  }
  // Pin and retain the box, then keep the pointer to its UIColor payload.
  // The old CreateUIColor() API returns that payload directly; it is valid
  // for an immediate value copy, but is not a MonoObject suitable for a GC
  // handle and can move if left unrooted.
  Reflect::Release(s_custom_time_color_handle);
  s_custom_time_color_handle = color_handle;
  s_custom_time_color_value = color_value;
  s_custom_time_color_r = r;
  s_custom_time_color_g = g;
  s_custom_time_color_b = b;
  return true;
}

void paint_label_white(MonoObject *label) {
  if (!label || !mono_gchandle_new || !mono_gchandle_get_target ||
      !mono_object_unbox)
    return;
  MonoObject *boxed = CreateUIColorBoxed(1.0f, 1.0f, 1.0f, 1.0f);
  if (!boxed)
    return;
  uint32_t handle = mono_gchandle_new(boxed, 1);
  MonoObject *pinned_box = Reflect::Target(handle);
  void *value = pinned_box ? mono_object_unbox(pinned_box) : nullptr;
  if (value)
    set_object_property_invoke(label, "TextColor", value);
  Reflect::Release(handle);
}

void mark_alias_customized(MonoObject *label, bool customized) {
  const int alias_index = find_clock_alias(label);
  if (alias_index >= 0)
    s_clock_aliases[static_cast<size_t>(alias_index)].customized = customized;
}

void restore_stock_layout(MonoObject *label, float x, float width,
                          float height) {
  set_object_property(label, "X", x);
  set_object_property(label, "Width", width);
  set_object_property(label, "Height", height);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "HorizontalAlignment", 2);
  set_object_property(label, "VerticalAlignment", 0);
  set_object_property(label, "FitWidthToText", false);
  set_object_property(label, "FitHeightToText", false);
  paint_label_white(label);
}

void restore_native_label(MonoObject *label) {
  if (!label || !s_native.customized)
    return;
  restore_stock_layout(label, s_native.base_x, s_native.base_width,
                       s_native.base_height);
  s_native.customized = false;
  s_native.visible_width = s_native.base_width;
  mark_alias_customized(label, false);
}

/* How wide `text` is in ems of the clock's font, an estimate. */
float text_em_width(const std::string &text) {
  float em = 0.0f;
  for (const unsigned char character : text) {
    if (character == ' ')
      em += 0.30f;
    else if (character == ':' || character == '.')
      em += 0.29f;
    else if (character >= '0' && character <= '9')
      em += 0.56f;
    else
      em += 0.58f;
  }
  return em;
}

/* How wide the label says its digits really are, 0 if it cannot say. */
float measured_text_width(MonoObject *label) {
  static MonoMethod *text_width = nullptr;
  static MonoClass *text_width_class = nullptr;
  MonoClass *label_class = mono_object_get_class(label);
  if (text_width_class != label_class) {
    text_width_class = label_class;
    text_width = Reflect::MethodOf(label_class, "GetTextWidth", 0);
  }
  if (!text_width)
    return 0.0f;
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(text_width, label, nullptr, &exception);
  return !exception && boxed && mono_object_unbox
             ? *static_cast<float *>(mono_object_unbox(boxed))
             : 0.0f;
}

/* Digits are not all the same width, so the measured value wobbles by a pixel
   or two every second and would make the date under it jitter. Keep the widest
   value seen for this format and size. */
float steady_text_width(float measured, const std::string &time_text) {
  static float widest = 0.0f;
  static size_t widest_glyphs = 0;
  static int widest_font = 0;
  const size_t glyphs = utf8_glyph_count(time_text);
  if (glyphs != widest_glyphs ||
      widest_font != g_settings.shell_clock_time_font_size) {
    widest_glyphs = glyphs;
    widest_font = g_settings.shell_clock_time_font_size;
    widest = 0.0f;
  }
  widest = std::max(widest, measured);
  return measured > 8.0f ? widest : measured;
}

/* Brings `label` up to date from s_native. False if the time is not in it. */
bool draw_clock_alias(MonoObject *label, NativeClockAlias &alias, bool active,
                      time_t now) {
  if (s_native.base_width <= 0.0f)
    return false;
  if (!custom_clock_drawn_for(label)) {
    restore_native_label(label);
    return false;
  }
  std::tm local{};
  if (!local_time(now, local))
    return false;
  const std::string rendered = format_time(local);
  if (raw_text(label) != rendered)
    write_label_text(label, rendered);
  dress_native_clock(label, rendered);
  alias.customized = s_native.customized;
  if (active) {
    publish_date_overlay(now, true);
    s_last_rendered = rendered;
    s_last_second = now;
  }
  return true;
}

std::string label_test_id(MonoObject *label) {
  MonoMethod *getter = Reflect::MethodOf(label, "get_TestId", 0);
  if (!getter)
    return {};
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(getter, label, nullptr, &exception);
  return exception ? std::string()
                   : Mono_to_String(reinterpret_cast<MonoString *>(value));
}

} // namespace

bool clock_animation_active() {
  return AniketosTheme::GlobalTheme::Animated() ||
         g_settings.shell_background_animation ==
             static_cast<int>(AniketosTheme::AnimationMode::Rainbow);
}

/* Which class in an object's hierarchy declares a property never changes, and
 * the clock asks the same dozen questions for every node of the parent chain
 * on every frame. Remember the answers (including "none") per concrete class.
 * Names are string literals of this file, so comparing pointers first is the
 * common hit. */
MonoClass *property_class(MonoObject *object, const char *name) {
  struct Entry {
    MonoClass *object_class;
    const char *name;
    MonoClass *declaring_class;
    bool used;
  };
  static Entry cache[128];
  MonoClass *object_class =
      object && mono_object_get_class ? mono_object_get_class(object) : nullptr;
  if (!object_class || !name)
    return resolve_property_class(object, name);
  const size_t start =
      ((reinterpret_cast<uintptr_t>(object_class) >> 4) ^
       (reinterpret_cast<uintptr_t>(name) >> 2)) % 128;
  for (size_t probe = 0; probe < 8; ++probe) {
    Entry &entry = cache[(start + probe) % 128];
    if (!entry.used) {
      entry = {object_class, name, resolve_property_class(object, name), true};
      return entry.declaring_class;
    }
    if (entry.object_class == object_class &&
        (entry.name == name || std::strcmp(entry.name, name) == 0))
      return entry.declaring_class;
  }
  return resolve_property_class(object, name);
}

float label_dimension(MonoObject *label, const char *property) {
  MonoClass *klass = property_class(label, property);
  return klass ? Get_Property<float>(klass, label, property) : 0.0f;
}

void prepare_space_tab_label(MonoObject *label, const std::string &stock) {
  if (!label)
    return;
  // The symbol is prepended to the existing UI3 label. Let the native layout
  // engine size both sibling tab labels to their complete strings; resizing
  // only Media can consume the shared row and collapse Games to an ellipsis.
  // Leave parent geometry, positions, styles, and focus widgets untouched.
  // Runtime property lookup makes unsupported variants a no-op.
  set_object_property(label, "FitWidthToText", true);
  set_object_property(label, "FitHeightToText", false);
  set_object_property(label, "TextTrimming", 0);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "MaxLineCount", 1);
  LOG_INFO("[HOME-SPACES:NATIVE] %s label auto-width enabled label=%p "
           "width=%.1f height=%.1f",
           AniketosTheme::SpaceSwitcherLabels::IsMediaTabLabel(stock)
               ? "media"
               : "games",
           static_cast<void *>(label),
           label_dimension(label, "Width"), label_dimension(label, "Height"));
}

// The clock's time in any system language (clock_text.hpp). The colon form
// ("22:18", "10:18 p. m.", "du.10:18") is every language's but Finnish. The
// dot form ("22.18", "9.05 PM") is Finnish only; on any other label it is a
// version or a size ("6.00", "10.40" on a game's Information screen), so it
// is taken only from the label Sony marks as the clock.
bool looks_like_stock_clock(MonoObject *label, const char *text) {
  if (ClockText::Parse(text))
    return true;
  return label &&
         ClockText::Parse(text, nullptr, ClockText::Separators::ClockLabel) &&
         has_clock_test_id(label);
}

bool custom_clock_on() {
  return AniketosTheme::CustomizationOn() && g_settings.shell_clock_enabled;
}

/* Whether the custom clock is drawn into `label` from here. */
bool custom_clock_drawn_for(MonoObject *label) {
  return custom_clock_on() && !(label && drawn_by_script(label));
}

/* ... into the clock being followed. */
bool custom_clock_drawn_here() { return custom_clock_drawn_for(clock_target()); }

bool plausible_native_clock_geometry(MonoObject *label) {
  if (!label)
    return false;
  const float width = label_dimension(label, "Width");
  const float height = label_dimension(label, "Height");
  const float y = label_dimension(label, "Y");
  if (width <= 0.0f || height <= 0.0f)
    return true;
  return width <= 220.0f && height >= 34.0f && height <= 82.0f &&
         y >= -12.0f && y <= 24.0f;
}

/* A label showing a time that is not taken for the clock (no test id, a
   size no stock clock has), said in the log a few times a session: without
   the clock, Home's root and what hangs there (the video, the picture) stay
   unknown. */
void note_unbound_clock(MonoObject *label, const std::string &stock) {
  static int noted = 0;
  static std::string last;
  if (noted >= 6 || stock == last)
    return;
  ++noted;
  last = stock;
  LOG_INFO("[CLOCK] '%s' looks like the time but is not taken for the clock: "
           "label=%p width=%.1f height=%.1f y=%.1f",
           stock.c_str(), static_cast<void *>(label),
           label_dimension(label, "Width"), label_dimension(label, "Height"),
           label_dimension(label, "Y"));
}

int find_clock_alias(MonoObject *label) {
  if (!label || !mono_gchandle_get_target)
    return -1;
  for (size_t index = 0; index < s_clock_aliases.size(); ++index) {
    NativeClockAlias &alias = s_clock_aliases[index];
    if (!alias.handle)
      continue;
    MonoObject *target = mono_gchandle_get_target(alias.handle);
    if (!target) {
      clear_alias(alias);
      continue;
    }
    if (target == label)
      return static_cast<int>(index);
  }
  return -1;
}

int remember_clock_alias(MonoObject *label, const std::string &stock) {
  const int existing = find_clock_alias(label);
  if (existing >= 0) {
    NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(existing)];
    alias.stock_time = stock;
    return existing;
  }
  if (!label || !mono_gchandle_new_weakref)
    return -1;
  for (NativeClockAlias &alias : s_clock_aliases) {
    if (!alias.handle || !mono_gchandle_get_target)
      continue;
    MonoObject *target = mono_gchandle_get_target(alias.handle);
    if (!target || HomeScene::IsDisposed(target))
      clear_alias(alias);
  }
  for (size_t index = 0; index < s_clock_aliases.size(); ++index) {
    NativeClockAlias &alias = s_clock_aliases[index];
    if (alias.handle)
      continue;
    const uint32_t handle = mono_gchandle_new_weakref(label, 0);
    if (!handle)
      return -1;
    alias.handle = handle;
    alias.base_x = label_dimension(label, "X");
    alias.base_width = label_dimension(label, "Width");
    alias.base_height = label_dimension(label, "Height");
    alias.stock_time = stock;
    alias.customized = false;
    if (alias.base_width > 0.0f)
      alias.stock_color = pinned_text_color(label);
    return static_cast<int>(index);
  }
  LOG_WARN("[CLOCK] native clock alias table full capacity=%zu",
           s_clock_aliases.size());
  return -1;
}

void load_clock_alias(const NativeClockAlias &alias) {
  s_native.stock_time = alias.stock_time;
  s_native.base_x = alias.base_x;
  s_native.base_width = alias.base_width;
  s_native.base_height = alias.base_height;
  s_native.customized = alias.customized;
}

void forget_clock_alias(MonoObject *label) {
  const int index = find_clock_alias(label);
  if (index < 0)
    return;
  clear_alias(s_clock_aliases[static_cast<size_t>(index)]);
}

MonoString *ui_string(const std::string &text) {
  if (!mono_string_new)
    return nullptr;
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  if (!domain && mono_get_root_domain)
    domain = mono_get_root_domain();
  return domain ? mono_string_new(domain, text.c_str()) : nullptr;
}

void write_label_text(MonoObject *label, MonoString *text) {
  if (s_native_label_set_text_orig)
    s_native_label_set_text_orig(label, text);
}

bool write_label_text(MonoObject *label, const std::string &text) {
  MonoString *managed = ui_string(text);
  if (managed)
    write_label_text(label, managed);
  return managed != nullptr;
}

MonoString *label_text(MonoObject *raw) {
  if (!raw || !mono_object_get_class || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return nullptr;
  MonoClass *klass = mono_object_get_class(raw);
  if (!klass)
    return nullptr;
  /* Label.OnUpdate asks this for every label: get_Text of the object's own
     class (not a parent's, as by name), looked up once per class */
  constexpr size_t kClasses = 8;
  static MonoClass *classes[kClasses] = {};
  static MonoMethod *getters[kClasses] = {};
  static size_t next = 0;
  size_t slot = 0;
  while (slot < kClasses && classes[slot] != klass)
    ++slot;
  if (slot == kClasses) {
    slot = next;
    next = (next + 1) % kClasses;
    classes[slot] = klass;
    getters[slot] = mono_class_get_method_from_name(klass, "get_Text", 0);
  }
  if (!getters[slot])
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value =
      mono_runtime_invoke(getters[slot], raw, nullptr, &exception);
  return exception ? nullptr : reinterpret_cast<MonoString *>(value);
}

std::string raw_text(MonoObject *raw) { return Mono_to_String(label_text(raw)); }

float native_clock_opacity(MonoObject *label) {
  if (!native_clock_visible(label))
    return 0.0f;
  if (MonoClass *klass = property_class(label, "FinalOpacity"))
    return std::clamp(Get_Property<float>(klass, label, "FinalOpacity"),
                      0.0f, 1.0f);
  if (MonoClass *klass = property_class(label, "Opacity"))
    return std::clamp(Get_Property<float>(klass, label, "Opacity"), 0.0f,
                      1.0f);
  return 1.0f;
}

void publish_date_overlay(time_t now, bool target_ready) {
  s_date_overlay.visible = false;
  s_date_overlay.wanted = false;
  s_date_overlay.text.clear();
  // UI3's FinalOpacity/IsFinallyShown can briefly report zero on this
  // firmware while the native clock is already painted.  Do not make the
  // independent date label disappear because of that transient state.
  /* The date belongs to the clock: it is exactly as opaque as the time and
     goes away with it. (It used to be forced to full opacity, which left it
     drawn on top of Settings, the Control Center and everything else.) The
     label that paints it smooths the value, so a one-frame dip in the native
     opacity during a scene swap does not make it blink. */
  s_date_overlay.opacity = target_ready && s_native.opacity > kShownOpacity
                               ? std::min(s_native.opacity, 1.0f)
                               : 0.0f;
  s_date_overlay.time_width = std::max(s_native.visible_width, 78.0f);
  s_date_overlay.time_center_x = s_native.center_x;
  s_date_overlay.time_bottom_y = s_native.bottom_y;
  /* The label is taller than the digits painted at its top; the date hangs
     off the digits, not off the box. */
  s_date_overlay.time_text_bottom_y =
      s_native.bottom_y - s_native.base_height +
      static_cast<float>(time_font_size()) * 1.32f;
  s_date_overlay.has_global_time_geometry = s_native.has_global_geometry;
  s_date_overlay.font_size =
      std::clamp(g_settings.shell_clock_date_font_size, 10, 28);
  s_date_overlay.date_spacing =
      std::clamp(g_settings.shell_clock_date_spacing, -80, 160);
  if (clock_animation_active()) {
    const AniketosTheme::RGB color = effective_clock_color();
    s_date_overlay.r = color_byte(color.r);
    s_date_overlay.g = color_byte(color.g);
    s_date_overlay.b = color_byte(color.b);
  } else {
    s_date_overlay.r = std::clamp(g_settings.shell_clock_date_r, 0, 255);
    s_date_overlay.g = std::clamp(g_settings.shell_clock_date_g, 0, 255);
    s_date_overlay.b = std::clamp(g_settings.shell_clock_date_b, 0, 255);
  }
  if (!target_ready || !custom_clock_drawn_here() ||
      !g_settings.shell_clock_show_date)
    return;
  std::tm local{};
  if (!local_time(now, local))
    return;
  s_date_overlay.text = format_date(local);
  s_date_overlay.wanted = !s_date_overlay.text.empty();
  s_date_overlay.visible =
      s_date_overlay.wanted && s_date_overlay.opacity > 0.0f;
}

void apply_custom_time_color(MonoObject *label) {
  if (ensure_custom_time_color())
    set_object_property_invoke(label, "TextColor", s_custom_time_color_value);
}

void restore_all_native_labels() {
  const float visible_width = s_native.visible_width;
  const bool customized = s_native.customized;
  MonoObject *active = clock_target();
  if (active && !HomeScene::IsDisposed(active))
    restore_native_label(active);
  for (NativeClockAlias &alias : s_clock_aliases) {
    if (!alias.handle || !mono_gchandle_get_target)
      continue;
    MonoObject *label = mono_gchandle_get_target(alias.handle);
    if (!label || label == active || HomeScene::IsDisposed(label) ||
        !alias.customized)
      continue;
    restore_stock_layout(label, alias.base_x, alias.base_width,
                         alias.base_height);
    alias.customized = false;
  }
  s_native.visible_width = visible_width;
  s_native.customized = customized && custom_clock_drawn_here();
}

void layout_native_clock(MonoObject *label, const std::string &time_text) {
  if (!label || s_native.base_width <= 0.0f)
    return;
  const size_t stock_glyphs =
      std::max<size_t>(utf8_glyph_count(s_native.stock_time), 4);
  const size_t rendered_glyphs =
      std::max<size_t>(utf8_glyph_count(time_text), stock_glyphs);
  const float ratio = static_cast<float>(rendered_glyphs) /
                      static_cast<float>(stock_glyphs);
  const float font_size = static_cast<float>(time_font_size());
  const float font_scale = font_size / 36.0f;
  // Size from the text being painted, not only from the old stock label.
  // Otherwise PUI wraps the longer custom value and renders the lower glyphs
  // as clipped dots on some ShellUI builds.
  const float estimated = text_em_width(time_text) * font_size;
  const float desired = std::clamp(
      std::max(s_native.base_width * ratio * font_scale * 1.16f,
               estimated + 40.0f),
      s_native.base_width, 480.0f);
  // Keep the expanded label's right edge aligned to the native clock
  // container. Extending it beyond that edge clips the final seconds digit
  // when the Control Center applies its parent clip rectangle.
  // The edge comes from the container React lays out, which we never resize:
  // the label's own remembered geometry can be one we applied earlier (a
  // label first met after it was already widened), and then every further
  // widening pushed the time past the right edge of the screen.
  float right_edge = s_native.base_x + s_native.base_width;
  if (MonoObject *holder = HomeScene::Parent(label)) {
    const float holder_width = label_dimension(holder, "Width");
    if (holder_width > 8.0f)
      right_edge = holder_width;
  }
  const float x = right_edge - desired;
  set_object_property(label, "RenderSync", true);
  set_object_property(label, "FitWidthToText", false);
  set_object_property(label, "FitHeightToText", false);
  set_object_property(label, "TextTrimming", 0);
  set_object_property(label, "MaxLineCount", 1);
  set_object_property(label, "HorizontalAlignment", 2);
  set_object_property(label, "VerticalAlignment", 0);
  set_object_property(label, "NumberOfLines", 1);
  set_object_property(label, "X", x);
  set_object_property(label, "Width", desired);
  if (clock_target() == label) {
    s_applied_x = x;
    s_applied_width = desired;
  }
  set_object_property(label, "Height",
                      std::max(s_native.base_height, font_size * 1.75f));
  // Keep the ShellUI clock's proven native font. The custom font path was the
  // source of firmware-dependent glyph wrapping; color and text formatting
  // remain fully controlled by Aniketos.
  apply_custom_time_color(label);
  s_native.customized = true;
  /* The native label is right-aligned and deliberately wider than its painted
     text to prevent trimming. The date must follow the painted time, not the
     centre of that safety box, so ask the label how wide the digits really
     are; the estimate is only a fallback. The date is centred on this, so a
     guess shows as an offset. */
  const float measured_width =
      steady_text_width(measured_text_width(label), time_text);
  s_measured_time_width = measured_width;
  s_native.visible_width = measured_width > 8.0f && measured_width <= desired
                               ? measured_width
                               : std::clamp(estimated, 52.0f, desired);
  s_native.has_global_geometry =
      global_label_geometry(label, desired, s_native.base_height,
                            s_native.center_x, s_native.bottom_y);
  if (s_native.has_global_geometry)
    s_native.center_x += (desired - s_native.visible_width) * 0.5f;
  mark_alias_customized(label, true);
}

void dress_native_clock(MonoObject *label, const std::string &time_text) {
  layout_native_clock(label, time_text);
  s_native.opacity = native_clock_opacity(label);
  s_native.visible = s_native.opacity > kShownOpacity;
}

/* A clock label is often first seen while its scene is still being built
 * (Home coming back from Settings or the Control Center builds a new one), and
 * at that point it has no size yet. The stock geometry used to be read once,
 * right then, so such a label kept a base width of zero for good: it was
 * never laid out, and the longer custom time was squeezed into Sony's box
 * where it wrapped out of sight. Pick the geometry up as soon as the label has
 * one, provided we have not resized it ourselves yet. */
void adopt_native_geometry(MonoObject *label, NativeClockAlias &alias,
                           bool active) {
  if (!label || alias.customized || alias.base_width > 0.0f)
    return;
  const float width = label_dimension(label, "Width");
  if (width <= 0.0f)
    return;
  alias.base_x = label_dimension(label, "X");
  alias.base_width = width;
  alias.base_height = label_dimension(label, "Height");
  if (!alias.stock_color)
    alias.stock_color = pinned_text_color(label);
  if (!active)
    return;
  s_native.base_x = alias.base_x;
  s_native.base_width = alias.base_width;
  s_native.base_height = alias.base_height;
  s_native.visible_width = alias.base_width;
  s_force_refresh = true;
}

bool update_clock_alias(MonoObject *label, size_t alias_index, time_t now) {
  if (!label || alias_index >= s_clock_aliases.size())
    return false;
  NativeClockAlias &alias = s_clock_aliases[alias_index];
  const bool active = clock_target() == label;
  adopt_native_geometry(label, alias, active);
  if (active)
    return draw_clock_alias(label, alias, true, now);
  NativeClockState followed = s_native;
  load_clock_alias(alias);
  const bool drawn = draw_clock_alias(label, alias, false, now);
  s_native = std::move(followed);
  return drawn;
}

bool restyled_by_react(MonoObject *target) {
  if (!s_native.customized || s_applied_width <= 0.0f)
    return false;
  const bool restyled =
      std::fabs(label_dimension(target, "Width") - s_applied_width) > 0.5f ||
      std::fabs(label_dimension(target, "X") - s_applied_x) > 0.5f;
  static int logged = 0;
  if (restyled && logged < 20) {
    ++logged;
    LOG_INFO("[CLOCK] React restyled the clock (width %.0f, ours %.0f); "
             "redrawn the same frame",
             label_dimension(target, "Width"), s_applied_width);
  }
  return restyled;
}

/* React put a text that is no time into a label dressed as a clock. The
   label Sony marks as the clock stays ours. Any other label is something
   else now, typically a list row: React keys rows by position, so when a row
   is inserted above (a game's Information screen adds "Installed in" a
   moment after it opens) each label below is handed the next row's text.
   It gets back its own size and colour and the time is never drawn in it
   again. */
void let_go_of_label(MonoObject *label, const std::string &text) {
  if (!label)
    return;
  const bool followed = clock_target() == label;
  const int index = find_clock_alias(label);
  if ((!followed && index < 0) || has_clock_test_id(label))
    return;
  if (index >= 0) {
    NativeClockAlias &alias = s_clock_aliases[static_cast<size_t>(index)];
    if (alias.customized && alias.base_width > 0.0f) {
      set_object_property(label, "X", alias.base_x);
      set_object_property(label, "Width", alias.base_width);
      set_object_property(label, "Height", alias.base_height);
    }
    MonoObject *color = Reflect::Target(alias.stock_color);
    if (color && mono_object_unbox)
      set_object_property_invoke(label, "TextColor", mono_object_unbox(color));
  }
  if (followed)
    release_clock();
  forget_clock_alias(label);
  static int logged = 0;
  if (logged < 10) {
    ++logged;
    LOG_INFO("[CLOCK] label %p was taken for a clock but now shows '%s'; "
             "given back",
             static_cast<void *>(label), text.c_str());
  }
}

bool has_clock_test_id(MonoObject *label) {
  const std::string id = label_test_id(label);
  return id == "clock-text" || id == std::string("clock-text") + kScriptClockMark;
}

/* The answer for the last label asked about is kept for half a second: the
   tick asks about the followed clock thirty times a second. */
bool drawn_by_script(MonoObject *label) {
  static MonoObject *asked = nullptr;
  static bool answer = false;
  static uint64_t asked_ms = 0;
  if (!label)
    return false;
  const uint64_t now_ms = MonotonicMs();
  if (label != asked || now_ms - asked_ms >= 500) {
    const std::string id = label_test_id(label);
    const size_t mark = sizeof(kScriptClockMark) - 1;
    answer = id.size() > mark &&
             id.compare(id.size() - mark, mark, kScriptClockMark) == 0;
    asked = label;
    asked_ms = now_ms;
  }
  return answer;
}

bool GetNativeClockOpacity(float *opacity) {
  MonoObject *target = clock_target();
  if (!opacity || !target)
    return false;
  /* s_native.opacity is only sampled at 30 Hz and says whether the clock is
     shown at all (parent chain, disposal). The value itself is read here on
     every call: one property of the label, which already includes every
     ancestor and the scene fade. */
  float live = s_native.opacity;
  if (s_native.opacity > 0.0f && !HomeScene::IsDisposed(target)) {
    if (MonoClass *klass = property_class(target, "FinalOpacity"))
      live = Get_Property<float>(klass, target, "FinalOpacity");
  }
  /* A Home that is merely covered is parked at about 0.02, not at 0; our
     labels sit on a layer above everything, so map that floor to nothing. */
  constexpr float kParked = 0.03f;
  *opacity = std::clamp((live - kParked) / (1.0f - kParked), 0.0f, 1.0f);
  return true;
}

} // namespace ShellClock
