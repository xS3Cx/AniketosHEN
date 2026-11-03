#include "aniketos_theme/toggle_styles.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "aniketos_ui/ui_color.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace AniketosTheme::ToggleStyles {
namespace {

using AniketosUI::UIColor;
namespace Reflect = AniketosUI::Reflect;

/* One drawn piece of a switch: a native panel with fully rounded ends,
   described at rest in both positions. Lengths are in the CSS pixels of
   the design the style follows; build() scales them into the system's
   64x32. Going from one position to the other every property moves along
   the piece's own timing curve (a CSS cubic-bezier) over its own duration. */
struct Look {
  float x, y, w, h;
  uint32_t fill; /* 0xAARRGGBB */
  uint32_t border;
};

struct Key {
  float at; /* 0..1 of the path's duration */
  float dx; /* added to x, in the design's pixels */
};

struct Piece {
  Look off, on;
  float border_width = 0.0f;
  float seconds = 0.2f;
  float delay = 0.0f;
  float curve[4] = {0.25f, 0.1f, 0.25f, 1.0f}; /* CSS "ease" */
  /* Colours on a clock of their own when color_seconds is not negative
     (a CSS step is a very short one after a delay). */
  float color_seconds = -1.0f;
  float color_delay = 0.0f;
  /* A CSS @keyframes excursion played on every change of position, on top
     of the move from one look to the other: an arm that reaches in and
     withdraws. Eased in and out between keys. */
  std::vector<Key> path;
  float path_seconds = 0.0f;
  /* Indicator's ring: fades out and back while it travels, and going off
     is not animated at all (the CSS animation only exists on the way on). */
  bool dip = false;
  bool snap_off = false;
};

struct Style {
  const char *name;
  float width, height; /* the design's own size, in its CSS pixels */
  std::vector<Piece> pieces; /* back to front */
  bool clip = false; /* nothing is drawn outside the switch's box */
};

constexpr float kEase[4] = {0.25f, 0.1f, 0.25f, 1.0f};
constexpr float kEaseIn[4] = {0.42f, 0.0f, 1.0f, 1.0f};
constexpr float kEaseOut[4] = {0.0f, 0.0f, 0.58f, 1.0f};
constexpr float kEaseInOut[4] = {0.42f, 0.0f, 0.58f, 1.0f};

Piece piece(Look off, Look on, float seconds, const float curve[4],
            float border_width = 0.0f) {
  Piece out;
  out.off = off;
  out.on = on;
  out.seconds = seconds;
  std::memcpy(out.curve, curve, sizeof(out.curve));
  out.border_width = border_width;
  return out;
}

/* The styles, after switches published on Uiverse.io. What the system's
   panels cannot draw is left out: soft and inner shadows, gradients (one
   flat tone between the two ends stands in), SVG artwork, text and
   animations that run on their own without the switch being touched. */
const std::vector<Style> &styles() {
  static const std::vector<Style> all = [] {
    std::vector<Style> out;

    { /* uiverse.io/Admin12121/massive-dodo-67 */
      Style style{"Indicator", 115.0f, 55.0f, {}};
      style.pieces.push_back(piece({0, 0, 115, 55, 0xff252532, 0xff32303e},
                                   {0, 0, 115, 55, 0xff252532, 0xff32303e},
                                   0.0f, kEase, 1.0f));
      Piece ring = piece({73.0f, 15, 25, 25, 0x00000000, 0xffef565f},
                         {14.3f, 15, 25, 25, 0x00000000, 0xff60d480}, 1.0f,
                         kEase, 3.0f);
      ring.dip = true;
      ring.snap_off = true;
      style.pieces.push_back(ring);
      /* thumb: linear-gradient(#3b3a4e, #272733), lit from above */
      style.pieces.push_back(piece({7.0f, 6.5f, 42, 42, 0xff313040, 0xff424151},
                                   {65.6f, 6.5f, 42, 42, 0xff313040, 0xff424151},
                                   0.3f, kEaseIn, 1.0f));
      out.push_back(style);
    }

    { /* the switch by zanina-yassine: grey to green, white thumb */
      Style style{"Classic", 51.0f, 31.0f, {}};
      style.pieces.push_back(piece({0, 0, 51, 31, 0xffe9e9eb, 0},
                                   {0, 0, 51, 31, 0xff34c759, 0}, 0.2f,
                                   kEaseOut));
      style.pieces.push_back(piece({2, 2, 27, 27, 0xffffffff, 0},
                                   {22, 2, 27, 27, 0xffffffff, 0}, 0.2f,
                                   kEaseOut));
      out.push_back(style);
    }

    { /* the brushed-metal switch by ashif_6672: a light rail with a blue
         and a grey stud joined by a bar, and a metal thumb that takes a blue
         rim when on. Its unit is 1vmin; the switch is 20 by 10. */
      Style style{"Metal", 20.0f, 10.0f, {}};
      /* rail: linear-gradient(0deg, #ededed, #bdbdbd) */
      style.pieces.push_back(piece({0, 0, 20, 10, 0xffd5d5d5, 0xffb0b0b0},
                                   {0, 0, 20, 10, 0xffd5d5d5, 0xffb0b0b0},
                                   0.0f, kEase, 0.1f));
      style.pieces.push_back(piece({2.5f, 2.5f, 5, 5, 0xff1b7fcf, 0},
                                   {2.5f, 2.5f, 5, 5, 0xff1b7fcf, 0}, 0.0f,
                                   kEase));
      style.pieces.push_back(piece({12.5f, 2.5f, 5, 5, 0xffa8a8a8, 0},
                                   {12.5f, 2.5f, 5, 5, 0xffa8a8a8, 0}, 0.0f,
                                   kEase));
      /* the bar between them: grey, with a blue part that grows from 45%
         to 65% of it (background-position 55% to 35%) */
      style.pieces.push_back(piece({7.5f, 4.75f, 5, 0.5f, 0xffaaaaaa, 0},
                                   {7.5f, 4.75f, 5, 0.5f, 0xffaaaaaa, 0},
                                   0.0f, kEase));
      style.pieces.push_back(piece({7.5f, 4.75f, 2.25f, 0.5f, 0xff2196f3, 0},
                                   {7.5f, 4.75f, 3.25f, 0.5f, 0xff2196f3, 0},
                                   0.5f, kEase));
      /* thumb: linear-gradient(#ededed, #bdbdbd); blue comes in at the rim */
      style.pieces.push_back(
          piece({0.333f, 0.25f, 9.375f, 9.375f, 0xffd9d9d9, 0xff8a8a8a},
                {10.292f, 0.25f, 9.375f, 9.375f, 0xffd9d9d9, 0xff2196f3},
                0.5f, kEase, 0.45f));
      out.push_back(style);
    }

    { /* the skull switch: a dark rail, a skull for a thumb and a bony arm
         that reaches in from the right when the switch is flipped. The
         skull waits half a second, then crosses in another half; it turns
         from grey to white (or back) at the three-quarter-second mark.
         20 by 10, in vmin. */
      Style style{"Skull", 20.0f, 10.0f, {}};
      style.clip = true; /* overflow: hidden, for the arm at rest */
      style.pieces.push_back(piece({0, 0, 20, 10, 0xff141414, 0xff000000},
                                   {0, 0, 20, 10, 0xff141414, 0xff000000},
                                   0.0f, kEase, 0.1f));
      /* the arm: two forearm bones and four fingers, parked 65% of the
         rail's width out to the right (@keyframes check-on) */
      const std::vector<Key> reach = {{0.0f, 13.0f},  {0.25f, 0.0f},
                                      {0.33f, 0.0f},  {0.66f, 10.0f},
                                      {0.80f, 10.0f}, {1.0f, 13.0f}};
      const auto bone = [&](float x, float y, float w, float h) {
        Piece part = piece({x + 13.0f, y, w, h, 0xffffffff, 0},
                           {x + 13.0f, y, w, h, 0xffffffff, 0}, 0.0f, kEase);
        part.path = reach;
        for (Key &key : part.path)
          key.dx -= 13.0f; /* the look already stands at the parked place */
        part.path_seconds = 1.5f;
        style.pieces.push_back(part);
      };
      bone(12.0f, 4.5f, 5.25f, 0.425f);
      bone(12.0f, 5.075f, 5.25f, 0.425f);
      bone(8.9f, 3.95f, 2.9f, 0.4f);
      bone(8.7f, 4.5f, 3.1f, 0.4f);
      bone(8.7f, 5.1f, 3.1f, 0.4f);
      bone(8.9f, 5.65f, 2.9f, 0.4f);
      /* the skull: cranium, two eye sockets, jaw */
      const auto skull = [&](float x, float y, float w, float h, uint32_t dim,
                             uint32_t lit) {
        Piece part = piece({0.6f + x, 0.6f + y, w, h, dim, 0},
                           {10.667f + x, 0.6f + y, w, h, lit, 0}, 0.5f,
                           kEaseInOut);
        part.delay = 0.5f;
        part.color_delay = 0.75f;
        part.color_seconds = 0.01f;
        style.pieces.push_back(part);
      };
      skull(0.0f, 0.0f, 8.75f, 7.0f, 0xff888888, 0xffffffff);
      skull(1.3f, 3.95f, 1.75f, 1.75f, 0xff141414, 0xff141414);
      skull(5.7f, 3.95f, 1.75f, 1.75f, 0xff141414, 0xff141414);
      skull(2.6f, 6.6f, 3.55f, 1.7f, 0xff888888, 0xffffffff);
      out.push_back(style);
    }

    { /* Indicator in its light form: white rail and thumb, the same ring */
      Style style{"Indicator Light", 115.0f, 55.0f, {}};
      style.pieces.push_back(piece({0, 0, 115, 55, 0xfff9fafb, 0x1a000000},
                                   {0, 0, 115, 55, 0xfff9fafb, 0x1a000000},
                                   0.0f, kEase, 1.0f));
      Piece ring = piece({74.0f, 15.5f, 24, 24, 0xe6ffffff, 0xffff5b61},
                         {15.3f, 15.5f, 24, 24, 0xe6ffffff, 0xff35c759}, 0.9f,
                         kEase, 3.0f);
      ring.dip = true;
      ring.snap_off = true;
      style.pieces.push_back(ring);
      style.pieces.push_back(piece({7.0f, 6.5f, 42, 42, 0xfff2f3f5, 0x14000000},
                                   {65.6f, 6.5f, 42, 42, 0xfff2f3f5, 0x14000000},
                                   0.28f, kEaseInOut, 1.0f));
      out.push_back(style);
    }

    return out;
  }();
  return all;
}

int wanted_style() {
  return CustomizationOn() ? g_settings.shell_toggle_style : 0;
}

const Style *chosen() {
  const int index = wanted_style() - 1;
  const std::vector<Style> &all = styles();
  return index >= 0 && index < static_cast<int>(all.size())
             ? &all[static_cast<size_t>(index)]
             : nullptr;
}

constexpr float kSwitchWidth = 64.0f, kSwitchHeight = 32.0f;
constexpr const char *kPieceName = "id_aniketos_toggle_piece";

struct Tracked {
  uint32_t owner = 0; /* weak: the switch */
  uint32_t frame = 0; /* weak: its own frame panel, once the system coloured it */
  uint32_t fill = 0;  /* weak: its thumb, the same */
  std::vector<uint32_t> pieces; /* weak: the panels the style added */
  int style = 0;
  bool on = false;
  uint64_t changed_ms = 0; /* when it changed position; 0: at rest */
  /* What the style took off the system's two panels, for when it goes. */
  UIColor frame_color{}, fill_color{};
  float frame_border = 0.0f, fill_border = 0.0f;
  bool frame_clip = false;
};

MonoClass *s_panel_class = nullptr;
std::vector<Tracked> s_tracked;
bool s_logged = false;
/* Above zero while this file is setting properties itself: its own panels
   pass through the same background hook and are to be left as they are. */
int s_own_calls = 0;
/* The colour space the system gives its own switch colours in; ours are
   handed over in the same one. */
int32_t s_color_space = 0;

MonoObject *call(MonoObject *object, const char *name, void **args = nullptr,
                 int argc = 0) {
  MonoMethod *found = Reflect::MethodOf(object, name, argc);
  if (!found)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(found, object, args, &exception);
  return exception ? nullptr : result;
}

template <typename Value>
void set_property(MonoObject *object, const char *setter, Value value) {
  void *args[1] = {&value};
  call(object, setter, args, 1);
}

bool boolean(MonoObject *object, const char *getter, bool fallback) {
  MonoObject *boxed = call(object, getter);
  return boxed ? *static_cast<bool *>(mono_object_unbox(boxed)) : fallback;
}

int integer(MonoObject *object, const char *getter, int fallback) {
  MonoObject *boxed = call(object, getter);
  return boxed ? *static_cast<int *>(mono_object_unbox(boxed)) : fallback;
}

/* Takes the style's panels off the switch again. */
void remove_pieces(Tracked &entry) {
  MonoObject *frame = Reflect::Target(entry.frame);
  for (uint32_t &handle : entry.pieces) {
    MonoObject *panel = Reflect::Target(handle);
    if (frame && panel) {
      void *args[1] = {panel};
      call(frame, "RemoveChild", args, 1);
    }
    Reflect::Release(handle);
  }
  entry.pieces.clear();
}

/* The system's own look back on a switch the style has left. */
void restore(Tracked &entry) {
  remove_pieces(entry);
  if (MonoObject *frame = Reflect::Target(entry.frame)) {
    set_property(frame, "set_ClipChildren", entry.frame_clip);
    set_property(frame, "set_BorderWidth", entry.frame_border);
    set_property(frame, "set_BackgroundColor", entry.frame_color);
  }
  if (MonoObject *fill = Reflect::Target(entry.fill)) {
    set_property(fill, "set_BorderWidth", entry.fill_border);
    set_property(fill, "set_BackgroundColor", entry.fill_color);
  }
}

void forget(Tracked &entry) {
  for (uint32_t &handle : entry.pieces)
    Reflect::Release(handle);
  Reflect::Release(entry.frame);
  Reflect::Release(entry.fill);
  Reflect::Release(entry.owner);
}

/* The entry of a switch, made the first time either of its panels is seen.
   Null when no more can be tracked: that one keeps the system's look. */
Tracked *tracked_switch(MonoObject *owner) {
  for (Tracked &entry : s_tracked)
    if (Reflect::Target(entry.owner) == owner)
      return &entry;
  /* drop the switches that are gone before taking a new one */
  for (size_t i = 0; i < s_tracked.size();) {
    if (Reflect::Target(s_tracked[i].owner)) {
      ++i;
      continue;
    }
    forget(s_tracked[i]);
    s_tracked.erase(s_tracked.begin() + static_cast<long>(i));
  }
  if (s_tracked.size() >= 96)
    return nullptr;
  Tracked fresh;
  Reflect::Hold(fresh.owner, owner);
  s_tracked.push_back(std::move(fresh));
  return &s_tracked.back();
}

/* The switch a frame or thumb panel belongs to, or null when the panel's
   parent is a check box of another kind (a tick box, a radio button). */
MonoObject *switch_of(MonoObject *panel) {
  MonoObject *parent = call(panel, "get_Parent");
  if (!parent)
    return nullptr;
  return integer(parent, "get_Style", -1) == 4 /* CheckBoxStyle.Switch */
             ? parent
             : nullptr;
}

/* A CSS cubic-bezier(x1, y1, x2, y2) timing function: the curve parameter
   for the given time is found by bisection (x is monotonic), and the curve's
   y there is the progress, which may pass 1 and come back. */
float timing(const float curve[4], float x) {
  if (x <= 0.0f)
    return 0.0f;
  if (x >= 1.0f)
    return 1.0f;
  const auto bezier = [](float t, float p1, float p2) {
    const float u = 1.0f - t;
    return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
  };
  float low = 0.0f, high = 1.0f, t = x;
  for (int i = 0; i < 18; ++i) {
    t = (low + high) * 0.5f;
    (bezier(t, curve[0], curve[2]) < x ? low : high) = t;
  }
  return bezier(t, curve[1], curve[3]);
}

UIColor color_of(uint32_t argb) {
  return {static_cast<float>((argb >> 16) & 0xff) / 255.0f,
          static_cast<float>((argb >> 8) & 0xff) / 255.0f,
          static_cast<float>(argb & 0xff) / 255.0f,
          static_cast<float>((argb >> 24) & 0xff) / 255.0f, s_color_space};
}

UIColor mix(uint32_t from, uint32_t to, float t) {
  const UIColor a = color_of(from), b = color_of(to);
  const float c = std::clamp(t, 0.0f, 1.0f);
  /* a colour fading in or out keeps the hue of the one that is visible */
  const UIColor hue_a = a.a > 0.0f ? a : b, hue_b = b.a > 0.0f ? b : a;
  return {hue_a.r + (hue_b.r - hue_a.r) * c, hue_a.g + (hue_b.g - hue_a.g) * c,
          hue_a.b + (hue_b.b - hue_a.b) * c, a.a + (b.a - a.a) * c,
          s_color_space};
}

/* Where a @keyframes excursion stands at `elapsed` seconds. */
float excursion(const Piece &part, float elapsed) {
  if (part.path.size() < 2 || part.path_seconds <= 0.0f)
    return 0.0f;
  const float at = std::clamp(elapsed / part.path_seconds, 0.0f, 1.0f);
  for (size_t i = 1; i < part.path.size(); ++i) {
    const Key &from = part.path[i - 1], &to = part.path[i];
    if (at > to.at && i + 1 < part.path.size())
      continue;
    const float span = to.at - from.at;
    const float local =
        span > 0.0f ? std::clamp((at - from.at) / span, 0.0f, 1.0f) : 1.0f;
    return from.dx + (to.dx - from.dx) * timing(kEaseInOut, local);
  }
  return part.path.back().dx;
}

/* Puts one piece where it is `elapsed` seconds after the switch changed
   position (or at rest in it, when settled). */
void place(MonoObject *panel, const Style &style, const Piece &part, bool on,
           float elapsed, bool settled) {
  float t = 1.0f, tint = 1.0f, opacity = 1.0f, shift = 0.0f;
  if (!settled && !(part.snap_off && !on)) {
    const float linear =
        part.seconds > 0.0f
            ? std::clamp((elapsed - part.delay) / part.seconds, 0.0f, 1.0f)
            : 1.0f;
    t = timing(part.curve, linear);
    tint = part.color_seconds >= 0.0f
               ? timing(kEase,
                        std::clamp((elapsed - part.color_delay) /
                                       std::max(part.color_seconds, 0.001f),
                                   0.0f, 1.0f))
               : std::clamp(t, 0.0f, 1.0f);
    if (part.dip && on)
      opacity = linear < 0.3f ? 1.0f - timing(kEase, linear / 0.3f)
                              : timing(kEase, (linear - 0.3f) / 0.7f);
    shift = excursion(part, elapsed);
  }
  const Look &from = on ? part.off : part.on;
  const Look &to = on ? part.on : part.off;
  const auto lerp = [t](float a, float b) { return a + (b - a) * t; };
  /* the design, scaled to fit the system's switch and set against its right
     edge, where the system's own sits in a row */
  const float scale = std::min(kSwitchWidth / style.width,
                               kSwitchHeight / style.height);
  const float left = kSwitchWidth - style.width * scale;
  const float top = (kSwitchHeight - style.height * scale) * 0.5f;
  const float width = std::max(0.0f, lerp(from.w, to.w)) * scale;
  const float height = std::max(0.0f, lerp(from.h, to.h)) * scale;
  set_property(panel, "set_X", left + (lerp(from.x, to.x) + shift) * scale);
  set_property(panel, "set_Y", top + lerp(from.y, to.y) * scale);
  set_property(panel, "set_Width", width);
  set_property(panel, "set_Height", height);
  set_property(panel, "set_BorderRadius", std::min(width, height) * 0.5f);
  set_property(panel, "set_BackgroundColor", mix(from.fill, to.fill, tint));
  if (part.border_width > 0.0f) {
    set_property(panel, "set_BorderWidth",
              std::max(1.0f, part.border_width * scale));
    set_property(panel, "set_BorderColor", mix(from.border, to.border, tint));
  }
  set_property(panel, "set_Opacity", opacity);
}

float longest(const Style &style) {
  float seconds = 0.0f;
  for (const Piece &part : style.pieces)
    seconds = std::max({seconds, part.delay + part.seconds,
                        part.color_seconds >= 0.0f
                            ? part.color_delay + part.color_seconds
                            : 0.0f,
                        part.path_seconds});
  return seconds;
}

void place_all(Tracked &entry, const Style &style, float elapsed,
               bool settled) {
  for (size_t i = 0; i < entry.pieces.size() && i < style.pieces.size(); ++i)
    if (MonoObject *panel = Reflect::Target(entry.pieces[i]))
      place(panel, style, style.pieces[i], entry.on, elapsed, settled);
}

bool build(Tracked &entry, MonoObject *frame, const Style &style) {
  if (!s_panel_class || !mono_gchandle_new_weakref)
    return false;
  for (size_t i = 0; i < style.pieces.size(); ++i) {
    MonoObject *panel = mono_object_new(mono_domain_get(), s_panel_class);
    if (!panel)
      return false;
    mono_runtime_object_init(panel);
    void *name[1] = {mono_string_new(mono_domain_get(), kPieceName)};
    call(panel, "set_Name", name, 1);
    void *child[1] = {panel};
    call(frame, "AppendChild", child, 1);
    entry.pieces.push_back(mono_gchandle_new_weakref(panel, false));
  }
  return true;
}

/* The system is colouring a switch's frame: make it ours. False when it could
   not be: the frame stays the system's. */
bool style_frame(Tracked &entry, MonoObject *frame, MonoObject *owner,
                 const Style &style) {
  const int wanted = wanted_style();
  const bool same_frame = Reflect::Target(entry.frame) == frame;
  const bool whole = same_frame && entry.style == wanted &&
                     entry.pieces.size() == style.pieces.size() &&
                     !entry.pieces.empty() &&
                     Reflect::Target(entry.pieces.front());
  if (!whole) {
    remove_pieces(entry);
    entry.style = wanted;
    if (!same_frame) {
      Reflect::Hold(entry.frame, frame);
      Reflect::FloatProperty(frame, "get_BorderWidth", &entry.frame_border);
      entry.frame_clip = boolean(frame, "get_ClipChildren", false);
    }
    if (!build(entry, frame, style)) {
      remove_pieces(entry);
      return false;
    }
    entry.on = boolean(owner, "get_Checked", false);
    entry.changed_ms = 0;
    place_all(entry, style, 0.0f, true);
  }
  set_property(frame, "set_BorderWidth", 0.0f);
  set_property(frame, "set_ClipChildren", style.clip);
  if (!s_logged) {
    s_logged = true;
    LOG_INFO("[TOGGLES] first switch styled style='%s' pieces=%zu",
             style.name, entry.pieces.size());
  }
  return true;
}

void strip_fill(Tracked &entry, MonoObject *fill) {
  if (Reflect::Target(entry.fill) != fill) {
    Reflect::Hold(entry.fill, fill);
    Reflect::FloatProperty(fill, "get_BorderWidth", &entry.fill_border);
  }
  set_property(fill, "set_BorderWidth", 0.0f);
}

} // namespace

void Install(MonoImage *pui) {
  s_panel_class = pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                             "Panel")
                      : nullptr;
  LOG_INFO("[TOGGLES] panel class=%d styles=%zu", s_panel_class ? 1 : 0,
           styles().size());
}

int Count() { return static_cast<int>(styles().size()) + 1; }

const char *Name(int style) {
  const std::vector<Style> &all = styles();
  return style >= 1 && style <= static_cast<int>(all.size())
             ? all[static_cast<size_t>(style - 1)].name
             : nullptr;
}

bool OnPanelBackground(MonoObject *panel, UIColor &color) {
  if (s_own_calls > 0)
    return true; /* a panel of ours: as set, untouched by any theme */
  const Style *style = chosen();
  if (!style || !panel || !s_panel_class || !mono_gchandle_new_weakref ||
      !shellui_hooks_are_ready())
    return false;
  ++s_own_calls;
  bool styled = false;
  const std::string name =
      Mono_to_String(reinterpret_cast<MonoString *>(call(panel, "get_Name")));
  const bool frame = name == "CheckBoxFrame";
  if (frame || name == "CheckBoxFill") {
    MonoObject *owner = switch_of(panel);
    if (Tracked *entry = owner ? tracked_switch(owner) : nullptr) {
      /* the system's two panels turn invisible; the style's own, on the
         frame, are what is seen */
      s_color_space = color.color_space;
      (frame ? entry->frame_color : entry->fill_color) = color;
      if (frame) {
        styled = style_frame(*entry, panel, owner, *style);
      } else {
        strip_fill(*entry, panel);
        styled = true;
      }
      if (styled)
        color.a = 0.0f;
    }
  }
  --s_own_calls;
  return styled;
}

void Tick() {
  if (s_tracked.empty())
    return;
  const Style *style = chosen();
  if (!style) {
    ++s_own_calls;
    for (Tracked &entry : s_tracked) {
      restore(entry);
      forget(entry);
    }
    s_tracked.clear();
    --s_own_calls;
    return;
  }
  const uint64_t now = AniketosUI::MonotonicMs();
  ++s_own_calls;
  for (Tracked &entry : s_tracked) {
    if (entry.style != wanted_style() || entry.pieces.empty())
      continue;
    MonoObject *frame = Reflect::Target(entry.frame);
    MonoObject *owner = frame ? call(frame, "get_Parent") : nullptr;
    if (!owner)
      continue;
    const bool on = boolean(owner, "get_Checked", entry.on);
    if (on != entry.on) {
      entry.on = on;
      entry.changed_ms = now;
    }
    if (!entry.changed_ms)
      continue;
    const float elapsed = static_cast<float>(now - entry.changed_ms) / 1000.0f;
    const bool done = elapsed >= longest(*style);
    place_all(entry, *style, elapsed, done);
    if (done)
      entry.changed_ms = 0;
  }
  --s_own_calls;
}

} // namespace AniketosTheme::ToggleStyles
