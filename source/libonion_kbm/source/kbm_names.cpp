/* Keyboard & mouse for games: JSON and display names (see kbm_names.hpp). */

#include <onion/kbm_names.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace onion::kbm {
namespace {

struct KeyName {
  uint8_t usage;
  const char *token;   /* after "key:" */
  const char *display;
};

/* USB HID usage page 7. Usages not listed are written as "key:0xNN". */
constexpr KeyName kKeys[] = {
    {0x04, "A", "A"}, {0x05, "B", "B"}, {0x06, "C", "C"}, {0x07, "D", "D"},
    {0x08, "E", "E"}, {0x09, "F", "F"}, {0x0a, "G", "G"}, {0x0b, "H", "H"},
    {0x0c, "I", "I"}, {0x0d, "J", "J"}, {0x0e, "K", "K"}, {0x0f, "L", "L"},
    {0x10, "M", "M"}, {0x11, "N", "N"}, {0x12, "O", "O"}, {0x13, "P", "P"},
    {0x14, "Q", "Q"}, {0x15, "R", "R"}, {0x16, "S", "S"}, {0x17, "T", "T"},
    {0x18, "U", "U"}, {0x19, "V", "V"}, {0x1a, "W", "W"}, {0x1b, "X", "X"},
    {0x1c, "Y", "Y"}, {0x1d, "Z", "Z"},
    {0x1e, "1", "1"}, {0x1f, "2", "2"}, {0x20, "3", "3"}, {0x21, "4", "4"},
    {0x22, "5", "5"}, {0x23, "6", "6"}, {0x24, "7", "7"}, {0x25, "8", "8"},
    {0x26, "9", "9"}, {0x27, "0", "0"},
    {0x28, "Enter", "Enter"}, {0x29, "Escape", "Esc"},
    {0x2a, "Backspace", "Backspace"}, {0x2b, "Tab", "Tab"},
    {0x2c, "Space", "Space"}, {0x2d, "Minus", "-"}, {0x2e, "Equal", "="},
    {0x2f, "LBracket", "["}, {0x30, "RBracket", "]"},
    {0x31, "Backslash", "\\"}, {0x32, "NonUSHash", "#"},
    {0x33, "Semicolon", ";"}, {0x34, "Quote", "'"}, {0x35, "Grave", "`"},
    {0x36, "Comma", ","}, {0x37, "Period", "."}, {0x38, "Slash", "/"},
    {0x39, "CapsLock", "Caps Lock"},
    {0x3a, "F1", "F1"}, {0x3b, "F2", "F2"}, {0x3c, "F3", "F3"},
    {0x3d, "F4", "F4"}, {0x3e, "F5", "F5"}, {0x3f, "F6", "F6"},
    {0x40, "F7", "F7"}, {0x41, "F8", "F8"}, {0x42, "F9", "F9"},
    {0x43, "F10", "F10"}, {0x44, "F11", "F11"}, {0x45, "F12", "F12"},
    {0x46, "PrintScreen", "Print Screen"}, {0x47, "ScrollLock", "Scroll Lock"},
    {0x48, "Pause", "Pause"}, {0x49, "Insert", "Insert"},
    {0x4a, "Home", "Home"}, {0x4b, "PageUp", "Page Up"},
    {0x4c, "Delete", "Delete"}, {0x4d, "End", "End"},
    {0x4e, "PageDown", "Page Down"}, {0x4f, "Right", "Right arrow"},
    {0x50, "Left", "Left arrow"}, {0x51, "Down", "Down arrow"},
    {0x52, "Up", "Up arrow"}, {0x53, "NumLock", "Num Lock"},
    {0x54, "KPDivide", "Numpad /"}, {0x55, "KPMultiply", "Numpad *"},
    {0x56, "KPMinus", "Numpad -"}, {0x57, "KPPlus", "Numpad +"},
    {0x58, "KPEnter", "Numpad Enter"},
    {0x59, "KP1", "Numpad 1"}, {0x5a, "KP2", "Numpad 2"},
    {0x5b, "KP3", "Numpad 3"}, {0x5c, "KP4", "Numpad 4"},
    {0x5d, "KP5", "Numpad 5"}, {0x5e, "KP6", "Numpad 6"},
    {0x5f, "KP7", "Numpad 7"}, {0x60, "KP8", "Numpad 8"},
    {0x61, "KP9", "Numpad 9"}, {0x62, "KP0", "Numpad 0"},
    {0x63, "KPPeriod", "Numpad ."}, {0x64, "NonUSBackslash", "\\ (ISO)"},
    {0x65, "Application", "Menu"}, {0x66, "Power", "Power"},
    {0x67, "KPEqual", "Numpad ="},
    {0x68, "F13", "F13"}, {0x69, "F14", "F14"}, {0x6a, "F15", "F15"},
    {0x6b, "F16", "F16"}, {0x6c, "F17", "F17"}, {0x6d, "F18", "F18"},
    {0x6e, "F19", "F19"}, {0x6f, "F20", "F20"}, {0x70, "F21", "F21"},
    {0x71, "F22", "F22"}, {0x72, "F23", "F23"}, {0x73, "F24", "F24"},
    {0x7f, "Mute", "Mute"}, {0x80, "VolumeUp", "Volume up"},
    {0x81, "VolumeDown", "Volume down"}, {0x85, "KPComma", "Numpad ,"},
    {0x87, "Ro", "Ro"}, {0x88, "Kana", "Katakana/Hiragana"},
    {0x89, "Yen", "Yen"}, {0x8a, "Henkan", "Henkan"},
    {0x8b, "Muhenkan", "Muhenkan"}, {0x90, "Lang1", "Hangul"},
    {0x91, "Lang2", "Hanja"},
    {0xe0, "LCtrl", "Left Ctrl"}, {0xe1, "LShift", "Left Shift"},
    {0xe2, "LAlt", "Left Alt"}, {0xe3, "LGui", "Left Win"},
    {0xe4, "RCtrl", "Right Ctrl"}, {0xe5, "RShift", "Right Shift"},
    {0xe6, "RAlt", "Right Alt"}, {0xe7, "RGui", "Right Win"},
};

struct IdName {
  Input id;
  const char *token; /* full, with prefix */
  const char *display;
};

constexpr IdName kMouseInputs[] = {
    {kMouseLeft, "mouse:left", "Left mouse button"},
    {kMouseRight, "mouse:right", "Right mouse button"},
    {kMouseMiddle, "mouse:middle", "Middle mouse button"},
    {kMouseBack, "mouse:back", "Mouse back"},
    {kMouseForward, "mouse:forward", "Mouse forward"},
    {0x0105, "mouse:6", "Mouse button 6"},
    {0x0106, "mouse:7", "Mouse button 7"},
    {0x0107, "mouse:8", "Mouse button 8"},
    {kWheelUp, "wheel:up", "Wheel up"},
    {kWheelDown, "wheel:down", "Wheel down"},
    {kWheelLeft, "wheel:left", "Wheel left"},
    {kWheelRight, "wheel:right", "Wheel right"},
    {kMouseMoveRight, "move:right", "Mouse moves right"},
    {kMouseMoveLeft, "move:left", "Mouse moves left"},
    {kMouseMoveDown, "move:down", "Mouse moves down"},
    {kMouseMoveUp, "move:up", "Mouse moves up"},
};

struct ButtonName {
  uint32_t bit;
  const char *token;
};

constexpr ButtonName kButtons[] = {
    {kPadCross, "cross"},     {kPadCircle, "circle"},
    {kPadSquare, "square"},   {kPadTriangle, "triangle"},
    {kPadL1, "l1"},           {kPadR1, "r1"},
    {kPadL2, "l2"},           {kPadR2, "r2"},
    {kPadL3, "l3"},           {kPadR3, "r3"},
    {kPadOptions, "options"}, {kPadTouchpad, "touchpad"},
    {kPadUp, "up"},           {kPadDown, "down"},
    {kPadLeft, "left"},       {kPadRight, "right"},
};

/* axis:<stick><axis><sign>, Y down positive */
struct AxisName {
  uint8_t axis;
  int sign;
  const char *token;
};
constexpr AxisName kAxes[] = {
    {kAxisLX, -1, "axis:lx-"}, {kAxisLX, +1, "axis:lx+"},
    {kAxisLY, -1, "axis:ly-"}, {kAxisLY, +1, "axis:ly+"},
    {kAxisRX, -1, "axis:rx-"}, {kAxisRX, +1, "axis:rx+"},
    {kAxisRY, -1, "axis:ry-"}, {kAxisRY, +1, "axis:ry+"},
};

bool prefixed(const char *s, const char *prefix, const char **rest) {
  const size_t n = strlen(prefix);
  if (strncasecmp(s, prefix, n) != 0) return false;
  *rest = s + n;
  return true;
}

template <typename E, size_t N>
bool parse_enum(const char *name, const char *const (&names)[N], E *out) {
  if (!name) return false;
  for (size_t i = 0; i < N; ++i) {
    if (strcasecmp(name, names[i]) == 0) {
      *out = static_cast<E>(i);
      return true;
    }
  }
  return false;
}

template <size_t N>
const char *enum_name(size_t i, const char *const (&names)[N]) {
  return i < N ? names[i] : "";
}

constexpr const char *kGestureNames[] = {"press", "tap", "long", "double",
                                         "release"};
constexpr const char *kModeNames[] = {"hold", "toggle", "turbo", "pulse"};
constexpr const char *kMouseModeNames[] = {"off", "velocity", "position"};
constexpr const char *kDeadzoneNames[] = {"circular", "square"};
constexpr const char *kOpposingNames[] = {"last", "neutral", "first"};

} // namespace

std::string InputName(Input input) {
  if (input >= kKeyFirst && input <= kKeyLast) {
    for (const KeyName &k : kKeys)
      if (k.usage == input) return std::string("key:") + k.token;
    char buf[16];
    snprintf(buf, sizeof(buf), "key:0x%02x", input);
    return buf;
  }
  for (const IdName &m : kMouseInputs)
    if (m.id == input) return m.token;
  return "";
}

bool ParseInput(const char *name, Input *out) {
  const char *rest = nullptr;
  if (!name || !out) return false;
  if (prefixed(name, "key:", &rest)) {
    if (strncasecmp(rest, "0x", 2) == 0 && rest[2]) {
      char *end = nullptr;
      const unsigned long v = strtoul(rest + 2, &end, 16);
      if (*end || v < kKeyFirst || v > kKeyLast) return false;
      *out = static_cast<Input>(v);
      return true;
    }
    for (const KeyName &k : kKeys) {
      if (strcasecmp(rest, k.token) == 0) {
        *out = k.usage;
        return true;
      }
    }
    return false;
  }
  for (const IdName &m : kMouseInputs) {
    if (strcasecmp(name, m.token) == 0) {
      *out = m.id;
      return true;
    }
  }
  return false;
}

std::string InputDisplayName(Input input) {
  if (input >= kKeyFirst && input <= kKeyLast) {
    for (const KeyName &k : kKeys)
      if (k.usage == input) return k.display;
    char buf[16];
    snprintf(buf, sizeof(buf), "Key 0x%02x", input);
    return buf;
  }
  for (const IdName &m : kMouseInputs)
    if (m.id == input) return m.display;
  return "";
}

std::string ButtonsName(uint32_t mask) {
  std::string out;
  for (const ButtonName &b : kButtons) {
    if (!(mask & b.bit)) continue;
    if (!out.empty()) out += '+';
    out += b.token;
  }
  return out;
}

bool ParseButtons(const char *names, uint32_t *out) {
  if (!names || !out || !*names) return false;
  uint32_t mask = 0;
  const char *p = names;
  while (*p) {
    const char *end = strchr(p, '+');
    const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    bool found = false;
    for (const ButtonName &b : kButtons) {
      if (strlen(b.token) == len && strncasecmp(p, b.token, len) == 0) {
        mask |= b.bit;
        found = true;
        break;
      }
    }
    if (!found) return false;
    p += len;
    if (*p == '+' && !*++p) return false; /* "cross+" */
  }
  *out = mask;
  return true;
}

std::string OutputName(const OutputDef &o) {
  switch (o.kind) {
  case OutputKind::None: return "none";
  case OutputKind::Button: return "pad:" + ButtonsName(o.buttons);
  case OutputKind::AxisDirection:
    for (const AxisName &a : kAxes)
      if (a.axis == o.axis && (a.sign < 0) == (o.value < 0)) return a.token;
    return "";
  case OutputKind::Trigger:
    return o.trigger == kTriggerL2 ? "trigger:l2" : "trigger:r2";
  case OutputKind::LayerHold: return "layer:hold";
  case OutputKind::LayerToggle: return "layer:toggle";
  case OutputKind::Macro: return "macro";
  case OutputKind::StickScale: return o.axis ? "scale:right" : "scale:left";
  case OutputKind::MappingToggle: return "suspend";
  }
  return "";
}

bool ParseOutput(const char *name, OutputDef *out) {
  if (!name || !out) return false;
  OutputDef o;
  const char *rest = nullptr;
  if (strcasecmp(name, "none") == 0) {
    o.kind = OutputKind::None;
  } else if (prefixed(name, "pad:", &rest)) {
    if (!ParseButtons(rest, &o.buttons)) return false;
    o.kind = OutputKind::Button;
  } else if (prefixed(name, "trigger:", &rest)) {
    if (strcasecmp(rest, "l2") == 0) o.trigger = kTriggerL2;
    else if (strcasecmp(rest, "r2") == 0) o.trigger = kTriggerR2;
    else return false;
    o.kind = OutputKind::Trigger;
    o.value = 32767;
  } else if (strcasecmp(name, "layer:hold") == 0) {
    o.kind = OutputKind::LayerHold;
  } else if (strcasecmp(name, "layer:toggle") == 0) {
    o.kind = OutputKind::LayerToggle;
  } else if (strcasecmp(name, "macro") == 0) {
    o.kind = OutputKind::Macro;
  } else if (strcasecmp(name, "scale:left") == 0 ||
             strcasecmp(name, "scale:right") == 0) {
    o.kind = OutputKind::StickScale;
    o.axis = strcasecmp(name, "scale:right") == 0;
    o.value = 16384; /* half: walk / precise aim */
  } else if (strcasecmp(name, "suspend") == 0) {
    o.kind = OutputKind::MappingToggle;
  } else {
    bool found = false;
    for (const AxisName &a : kAxes) {
      if (strcasecmp(name, a.token) == 0) {
        o.kind = OutputKind::AxisDirection;
        o.axis = a.axis;
        o.value = static_cast<int16_t>(a.sign * 32767);
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  *out = o;
  return true;
}

const char *GestureName(Gesture g) { return enum_name(static_cast<size_t>(g), kGestureNames); }
bool ParseGesture(const char *n, Gesture *o) { return parse_enum(n, kGestureNames, o); }
const char *ModeName(Mode m) { return enum_name(static_cast<size_t>(m), kModeNames); }
bool ParseMode(const char *n, Mode *o) { return parse_enum(n, kModeNames, o); }
const char *MouseModeName(MouseMode m) { return enum_name(static_cast<size_t>(m), kMouseModeNames); }
bool ParseMouseMode(const char *n, MouseMode *o) { return parse_enum(n, kMouseModeNames, o); }
const char *DeadzoneShapeName(DeadzoneShape s) { return enum_name(static_cast<size_t>(s), kDeadzoneNames); }
bool ParseDeadzoneShape(const char *n, DeadzoneShape *o) { return parse_enum(n, kDeadzoneNames, o); }
const char *OpposingName(Opposing o) { return enum_name(static_cast<size_t>(o), kOpposingNames); }
bool ParseOpposing(const char *n, Opposing *o) { return parse_enum(n, kOpposingNames, o); }

} // namespace onion::kbm
