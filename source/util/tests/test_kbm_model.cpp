/* Host tests for the KB+M profile model: names, JSON, validation, Compile
 * and the store. Written from kbm_types.hpp and the W1 headers. */
#include "test_harness.h"

#include <onion/kbm_names.hpp>
#include <onion/kbm_profile.hpp>
#include <onion/kbm_store.hpp>
#include <onion/kbm_types.hpp>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

using namespace onion::kbm;

/* Copies both sides: the harness macro keeps a pointer, which dangles for
   a temporary std::string's c_str(). */
#define KSTREQ(expected, actual)                                               \
  do {                                                                         \
    const std::string e__(expected), a__(actual);                              \
    if (e__ != a__)                                                            \
      return onion_test_fail(__FILE__, __LINE__, "expected \"%s\", got \"%s\"", \
                             e__.c_str(), a__.c_str());                        \
  } while (0)

namespace {

constexpr Input kA = 0x04, kE = 0x08, kM = 0x10, kQ = 0x14, kW = 0x1a,
                kF12 = 0x45, kPause = 0x48, kLShift = 0xe1;

OutputDef Btn(uint32_t mask) {
  OutputDef o;
  o.kind = OutputKind::Button;
  o.buttons = mask;
  return o;
}
OutputDef Named(OutputKind kind, const char *layer_or_macro) {
  OutputDef o;
  o.kind = kind;
  if (kind == OutputKind::Macro) o.macro = layer_or_macro;
  else o.layer = layer_or_macro;
  return o;
}
BindingDef Bind(Input in, OutputDef o) {
  BindingDef b;
  b.input = in;
  b.outputs.push_back(o);
  return b;
}
MacroStep Step(uint32_t buttons, uint16_t ms) {
  MacroStep s{};
  s.buttons = buttons;
  for (int a = 0; a < 4; ++a) s.axes[a] = INT16_MIN;
  s.duration_ms = ms;
  return s;
}

/* A profile that uses every field with non-default values. */
Profile RichProfile() {
  Profile p;
  p.name = "Rich \"quoted\" name";
  p.enabled = false;
  p.merge_real_pad = false;
  p.trigger_button_threshold = 77;

  LayerDef base;
  base.name = "base";
  base.mouse.mode = MouseMode::Velocity;
  base.mouse.stick = 1;
  base.mouse.deadzone_shape = DeadzoneShape::Square;
  base.mouse.invert = 2;
  base.mouse.sensitivity_x100 = 135;
  base.mouse.yx_ratio_x100 = 80;
  base.mouse.exponent_x100 = 120;
  base.mouse.deadzone_x1000 = 220;
  base.mouse.outer_x1000 = 950;
  base.mouse.smoothing_ms = 24;
  base.mouse.boost_x100 = 130;
  base.mouse.position_range = 800;
  base.mouse.spring_per_s_x10 = 35;
  base.opposing = Opposing::Neutral;
  base.mouse_digital_threshold = 3000;

  BindingDef w = Bind(kW, Btn(kPadCross | kPadL1));
  w.modifiers[0] = kLShift;
  w.modifiers[1] = kMouseRight;
  w.gesture = Gesture::DoubleTap;
  w.mode = Mode::Turbo;
  w.turbo_hz = 17;
  w.turbo_duty = 35;
  w.tap_ms = 150;
  w.long_ms = 500;
  w.double_ms = 300;
  w.pulse_ms = 80;
  w.label = "Sprint";
  base.bindings.push_back(w);

  OutputDef steer;
  steer.kind = OutputKind::AxisDirection;
  steer.axis = kAxisLX;
  steer.value = -16384;
  steer.ramp = Ramp{120, 90, 100, 0};
  base.bindings.push_back(Bind(kA, steer));

  OutputDef throttle;
  throttle.kind = OutputKind::Trigger;
  throttle.trigger = kTriggerR2;
  throttle.value = 24576;
  throttle.ramp = Ramp{300, 150, 250, 0};
  BindingDef t = Bind(kE, throttle);
  t.gesture = Gesture::LongPress;
  t.outputs.push_back(Btn(kPadSquare));
  base.bindings.push_back(t);

  OutputDef walk;
  walk.kind = OutputKind::StickScale;
  walk.axis = 0;
  walk.value = 13107;
  base.bindings.push_back(Bind(0xe2, walk));

  base.bindings.push_back(Bind(kQ, Named(OutputKind::LayerHold, "aim")));
  base.bindings.push_back(Bind(0x2b, Named(OutputKind::LayerToggle, "car")));
  BindingDef m = Bind(kM, Named(OutputKind::Macro, "combo"));
  m.mode = Mode::Pulse;
  base.bindings.push_back(m);
  OutputDef none;
  none.kind = OutputKind::None;
  base.bindings.push_back(Bind(kWheelDown, none));
  OutputDef sus;
  sus.kind = OutputKind::MappingToggle;
  base.bindings.push_back(Bind(kF12, sus));
  BindingDef rel = Bind(kMouseForward, Btn(kPadTouchpad));
  rel.gesture = Gesture::Release;
  rel.mode = Mode::Toggle;
  base.bindings.push_back(rel);
  base.bindings.push_back(Bind(kMouseMoveUp, Btn(kPadUp)));
  base.bindings.push_back(Bind(0x87, Btn(kPadOptions)));

  LayerDef aim;
  aim.name = "aim";
  aim.mouse_inherit = true;
  aim.opposing = Opposing::FirstWins;
  BindingDef tap = Bind(kW, Btn(kPadR3));
  tap.gesture = Gesture::Tap;
  aim.bindings.push_back(tap);

  LayerDef car;
  car.name = "car";
  car.mouse.mode = MouseMode::Position;
  car.mouse.stick = 0;
  car.opposing = Opposing::LastWins;

  p.layers = {base, aim, car};

  MacroDef combo;
  combo.name = "combo";
  combo.repeat_while_held = true;
  MacroStep s1 = Step(kPadL3 | kPadR3, 80);
  s1.axes[kAxisRX] = 32767;
  s1.axes[kAxisLY] = -32767;
  s1.l2 = 255;
  s1.r2 = 12;
  combo.steps = {s1, Step(0, 40), Step(kPadCircle, 16)}; /* 16 ms: the shortest step */
  MacroDef other;
  other.name = "other";
  other.steps = {Step(kPadTriangle, 30)};
  p.macros = {other, combo};
  return p;
}

bool HasBinding(const ProfileBlob &b, Input in, uint8_t layer, const Binding **out = nullptr) {
  for (int i = 0; i < b.binding_count && i < kMaxBindings; ++i)
    if (b.bindings[i].input == in && b.bindings[i].layer == layer) {
      if (out) *out = &b.bindings[i];
      return true;
    }
  return false;
}

std::unique_ptr<ProfileBlob> Blob(uint8_t fill) {
  auto b = std::make_unique<ProfileBlob>();
  std::memset(b.get(), fill, sizeof(ProfileBlob));
  return b;
}

/* Every binding, macro and layer reference in a compiled blob is in range. */
int CheckBlobSane(const ProfileBlob &b) {
  TEST_ASSERT_EQ_U64(kBlobMagic, b.magic);
  TEST_ASSERT_EQ_INT(kBlobVersion, b.version);
  TEST_ASSERT_EQ_INT((int)sizeof(ProfileBlob), b.size);
  TEST_ASSERT_TRUE(b.layer_count >= 1 && b.layer_count <= kMaxLayers);
  TEST_ASSERT_TRUE(b.binding_count <= kMaxBindings);
  TEST_ASSERT_TRUE(b.macro_count <= kMaxMacros);
  TEST_ASSERT_TRUE(b.macro_step_count <= kMaxMacroSteps);
  for (int i = 0; i < b.macro_count; ++i) {
    TEST_ASSERT_TRUE(b.macros[i].first_step + b.macros[i].step_count <= b.macro_step_count);
  }
  for (int i = 0; i < b.binding_count; ++i) {
    const Binding &x = b.bindings[i];
    TEST_ASSERT_TRUE(x.input != kInputNone && x.input < kInputCount);
    TEST_ASSERT_TRUE(x.input != b.suspend_key);
    TEST_ASSERT_TRUE(x.layer < b.layer_count);
    TEST_ASSERT_TRUE(x.output_count >= 1 && x.output_count <= kMaxOutputsPerBinding);
    TEST_ASSERT_TRUE((uint8_t)x.gesture <= (uint8_t)Gesture::Release);
    TEST_ASSERT_TRUE((uint8_t)x.mode <= (uint8_t)Mode::Pulse);
    TEST_ASSERT_TRUE(x.turbo_hz >= 1 && x.turbo_hz <= 30);
    TEST_ASSERT_TRUE(x.turbo_duty >= 10 && x.turbo_duty <= 90);
    for (int k = 0; k < x.output_count; ++k) {
      const Output &o = x.outputs[k];
      TEST_ASSERT_TRUE((uint8_t)o.kind <= (uint8_t)OutputKind::MappingToggle);
      if (o.kind == OutputKind::LayerHold || o.kind == OutputKind::LayerToggle)
        TEST_ASSERT_TRUE(o.layer < b.layer_count);
      if (o.kind == OutputKind::Macro) TEST_ASSERT_TRUE(o.macro < b.macro_count);
      if (o.kind == OutputKind::AxisDirection) TEST_ASSERT_TRUE(o.axis <= kAxisRY && o.value >= -32767);
      if (o.kind == OutputKind::Trigger) TEST_ASSERT_TRUE(o.trigger <= kTriggerR2 && o.value >= 0);
      if (o.kind == OutputKind::StickScale) TEST_ASSERT_TRUE(o.axis <= 1 && o.value >= 0);
      if (o.kind == OutputKind::Button)
        TEST_ASSERT_TRUE(o.buttons != 0 && (o.buttons & kPadIntercepted) == 0);
    }
  }
  for (int i = 0; i < b.layer_count; ++i) {
    const MouseParams &m = b.layers[i].mouse;
    TEST_ASSERT_TRUE(m.deadzone_x1000 <= 500);
    TEST_ASSERT_TRUE(m.outer_x1000 >= 500 && m.outer_x1000 <= 1000);
    TEST_ASSERT_TRUE(m.smoothing_ms >= 4 && m.smoothing_ms <= 100);
    TEST_ASSERT_TRUE(m.stick <= 1);
    TEST_ASSERT_TRUE(std::memchr(b.layers[i].name, 0, sizeof(b.layers[i].name)) != nullptr);
  }
  return 0;
}

std::string TempDir() {
  char tmpl[] = "/tmp/kbm_testXXXXXX";
  const char *d = mkdtemp(tmpl);
  return d ? d : "";
}

} // namespace

/* ---- names ----------------------------------------------------------- */

static int test_names_every_input(void) {
  int named = 0;
  for (int id = 0; id < 0x200; ++id) {
    const Input in = Input(id);
    const bool valid = (id >= kKeyFirst && id <= kKeyLast) ||
                       (id >= kMouseButtonFirst && id <= kMouseButtonLast) ||
                       (id >= kWheelUp && id <= kWheelRight) ||
                       (id >= kMouseMoveRight && id <= kMouseMoveUp);
    const std::string name = InputName(in);
    if (!valid) {
      if (!name.empty())
        return onion_test_fail(__FILE__, __LINE__, "0x%x named \"%s\"", id, name.c_str());
      continue;
    }
    if (name.empty()) return onion_test_fail(__FILE__, __LINE__, "0x%x has no name", id);
    if (InputDisplayName(in).empty())
      return onion_test_fail(__FILE__, __LINE__, "0x%x has no display name", id);
    Input back = 0;
    if (!ParseInput(name.c_str(), &back) || back != in)
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" parsed to 0x%x, want 0x%x",
                             name.c_str(), back, id);
    ++named;
  }
  TEST_ASSERT_EQ_INT(255 + 8 + 4 + 4, named);
  return 0;
}

static int test_names_spellings(void) {
  struct Case { const char *name; Input want; };
  const Case ok[] = {
      {"key:W", kW},          {"key:w", kW},           {"KEY:W", kW},
      {"key:LShift", kLShift}, {"key:lshift", kLShift}, {"key:0x87", 0x87},
      {"key:0X87", 0x87},     {"key:Pause", kPause},   {"mouse:left", kMouseLeft},
      {"mouse:right", kMouseRight}, {"mouse:middle", kMouseMiddle},
      {"mouse:back", kMouseBack}, {"mouse:forward", kMouseForward},
      {"mouse:6", 0x105},     {"mouse:8", 0x107},      {"wheel:up", kWheelUp},
      {"wheel:down", kWheelDown}, {"wheel:left", kWheelLeft},
      {"wheel:right", kWheelRight}, {"move:right", kMouseMoveRight},
      {"move:left", kMouseMoveLeft}, {"move:down", kMouseMoveDown},
      {"move:up", kMouseMoveUp},
  };
  for (const Case &c : ok) {
    Input got = 0;
    if (!ParseInput(c.name, &got) || got != c.want)
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" -> 0x%x, want 0x%x", c.name, got, c.want);
  }
  KSTREQ("key:W", InputName(kW).c_str());
  KSTREQ("mouse:left", InputName(kMouseLeft).c_str());
  KSTREQ("wheel:up", InputName(kWheelUp).c_str());
  KSTREQ("move:right", InputName(kMouseMoveRight).c_str());

  const char *bad[] = {"", "key:", "key:0x0", "key:0x00", "key:0x100", "key:0x",
                       "key:0xzz", "key:Bogus", "W", "mouse:", "mouse:0",
                       "mouse:9", "mouse:leftt", "wheel:sideways", "move:",
                       "pad:cross", "key:W ", " key:W", "key:W\n",
                       "key:-1", "key:0x87junk", "mouse:6x"};
  for (const char *b : bad) {
    Input got = 0x1234;
    if (ParseInput(b, &got))
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" accepted as 0x%x", b, got);
  }
  Input got = 0;
  TEST_ASSERT_TRUE(!ParseInput(nullptr, &got));
  return 0;
}

static int test_names_buttons(void) {
  const uint32_t bits[] = {kPadL3, kPadR3, kPadOptions, kPadUp, kPadRight, kPadDown,
                           kPadLeft, kPadL2, kPadR2, kPadL1, kPadR1, kPadTriangle,
                           kPadCircle, kPadCross, kPadSquare, kPadTouchpad};
  uint32_t all = 0;
  for (uint32_t b : bits) {
    all |= b;
    const std::string n = ButtonsName(b);
    uint32_t back = 0;
    if (n.empty() || !ParseButtons(n.c_str(), &back) || back != b)
      return onion_test_fail(__FILE__, __LINE__, "button 0x%x \"%s\" -> 0x%x", b, n.c_str(), back);
  }
  uint32_t back = 0;
  TEST_ASSERT_TRUE(ParseButtons(ButtonsName(all).c_str(), &back));
  TEST_ASSERT_EQ_U64(all, back);
  TEST_ASSERT_TRUE(ParseButtons("cross+l1", &back));
  TEST_ASSERT_EQ_U64(kPadCross | kPadL1, back);
  TEST_ASSERT_TRUE(ParseButtons("CROSS", &back));
  TEST_ASSERT_EQ_U64(kPadCross, back);
  const char *bad[] = {"", "cross+", "+cross", "cross++l1", "cross+foo", "foo", "intercepted"};
  for (const char *b : bad) {
    if (ParseButtons(b, &back))
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" accepted as 0x%x", b, back);
  }
  /* The intercepted bit is the system's: never a name. */
  TEST_ASSERT_TRUE(ButtonsName(kPadIntercepted).find("intercept") == std::string::npos);
  return 0;
}

static int test_names_outputs(void) {
  struct Case { const char *name; OutputKind kind; uint32_t buttons; uint8_t axis, trigger; int value; };
  const Case ok[] = {
      {"none", OutputKind::None, 0, 0, 0, 0},
      {"pad:cross", OutputKind::Button, kPadCross, 0, 0, 0},
      {"pad:l3+r3", OutputKind::Button, kPadL3 | kPadR3, 0, 0, 0},
      {"axis:lx-", OutputKind::AxisDirection, 0, kAxisLX, 0, -32767},
      {"axis:lx+", OutputKind::AxisDirection, 0, kAxisLX, 0, 32767},
      {"axis:ly-", OutputKind::AxisDirection, 0, kAxisLY, 0, -32767},
      {"axis:rx+", OutputKind::AxisDirection, 0, kAxisRX, 0, 32767},
      {"axis:ry-", OutputKind::AxisDirection, 0, kAxisRY, 0, -32767},
      {"trigger:l2", OutputKind::Trigger, 0, 0, kTriggerL2, 32767},
      {"trigger:r2", OutputKind::Trigger, 0, 0, kTriggerR2, 32767},
      {"layer:hold", OutputKind::LayerHold, 0, 0, 0, 0},
      {"layer:toggle", OutputKind::LayerToggle, 0, 0, 0, 0},
      {"macro", OutputKind::Macro, 0, 0, 0, 0},
      {"scale:left", OutputKind::StickScale, 0, 0, 0, -1},
      {"scale:right", OutputKind::StickScale, 0, 1, 0, -1},
      {"suspend", OutputKind::MappingToggle, 0, 0, 0, 0},
  };
  for (const Case &c : ok) {
    OutputDef o;
    o.layer = "stale";
    if (!ParseOutput(c.name, &o))
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" rejected", c.name);
    if (o.kind != c.kind || o.buttons != c.buttons)
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" kind %d buttons 0x%x", c.name,
                             (int)o.kind, o.buttons);
    if (c.kind == OutputKind::AxisDirection && (o.axis != c.axis || o.value != c.value))
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" axis %d value %d", c.name, o.axis, o.value);
    if (c.kind == OutputKind::Trigger && (o.trigger != c.trigger || o.value != c.value))
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" trigger %d value %d", c.name, o.trigger, o.value);
    if (c.kind == OutputKind::StickScale && o.axis != c.axis)
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" stick %d", c.name, o.axis);
    if (!o.layer.empty()) return onion_test_fail(__FILE__, __LINE__, "\"%s\" kept old layer", c.name);
    KSTREQ(c.name, OutputName(o).c_str());
    TEST_ASSERT_TRUE(!OutputDisplayName(o).empty());
  }
  OutputDef o;
  TEST_ASSERT_TRUE(ParseOutput("PAD:Cross", &o));
  TEST_ASSERT_EQ_U64(kPadCross, o.buttons);
  const char *bad[] = {"", "pad:", "pad:foo", "axis:lz+", "axis:lx", "axis:lx*",
                       "trigger:l3", "trigger:", "layer:", "layer:set", "scale:up",
                       "bogus", "key:W", "pad:cross+"};
  for (const char *b : bad) {
    if (ParseOutput(b, &o)) return onion_test_fail(__FILE__, __LINE__, "\"%s\" accepted", b);
  }
  /* The picker lists only names that parse. */
  const auto all = AllOutputNames();
  TEST_ASSERT_TRUE(all.size() >= 16 + 8 + 2);
  for (const std::string &n : all)
    if (!ParseOutput(n.c_str(), &o))
      return onion_test_fail(__FILE__, __LINE__, "picker name \"%s\" does not parse", n.c_str());
  return 0;
}

static int test_names_enums(void) {
  for (int i = 0; i <= (int)Gesture::Release; ++i) {
    Gesture g{};
    TEST_ASSERT_TRUE(ParseGesture(GestureName(Gesture(i)), &g) && g == Gesture(i));
  }
  for (int i = 0; i <= (int)Mode::Pulse; ++i) {
    Mode m{};
    TEST_ASSERT_TRUE(ParseMode(ModeName(Mode(i)), &m) && m == Mode(i));
  }
  for (int i = 0; i <= (int)MouseMode::Position; ++i) {
    MouseMode m{};
    TEST_ASSERT_TRUE(ParseMouseMode(MouseModeName(MouseMode(i)), &m) && m == MouseMode(i));
  }
  for (int i = 0; i <= (int)DeadzoneShape::Square; ++i) {
    DeadzoneShape s{};
    TEST_ASSERT_TRUE(ParseDeadzoneShape(DeadzoneShapeName(DeadzoneShape(i)), &s) && s == DeadzoneShape(i));
  }
  for (int i = 0; i <= (int)Opposing::FirstWins; ++i) {
    Opposing o{};
    TEST_ASSERT_TRUE(ParseOpposing(OpposingName(Opposing(i)), &o) && o == Opposing(i));
  }
  /* Out of range never crashes and never returns null. */
  TEST_ASSERT_TRUE(GestureName(Gesture(200)) != nullptr);
  TEST_ASSERT_TRUE(ModeName(Mode(200)) != nullptr);
  TEST_ASSERT_TRUE(MouseModeName(MouseMode(200)) != nullptr);
  TEST_ASSERT_TRUE(DeadzoneShapeName(DeadzoneShape(200)) != nullptr);
  TEST_ASSERT_TRUE(OpposingName(Opposing(200)) != nullptr);
  Gesture g{};
  TEST_ASSERT_TRUE(!ParseGesture("wiggle", &g));
  TEST_ASSERT_TRUE(!ParseGesture("", &g));
  TEST_ASSERT_TRUE(!ParseGesture(nullptr, &g));
  Mode m{};
  TEST_ASSERT_TRUE(!ParseMode("sometimes", &m));
  return 0;
}

/* ---- JSON ------------------------------------------------------------ */

static int test_json_round_trip(void) {
  const Profile p = RichProfile();
  const std::string j1 = ProfileToJson(p);
  Profile q;
  Warnings w;
  std::string err;
  if (!ProfileFromJson(j1.c_str(), j1.size(), &q, &w, &err))
    return onion_test_fail(__FILE__, __LINE__, "parse failed: %s", err.c_str());
  if (!w.empty())
    return onion_test_fail(__FILE__, __LINE__, "%zu warnings, first: %s", w.size(), w[0].c_str());
  KSTREQ(j1.c_str(), ProfileToJson(q).c_str());

  KSTREQ(p.name.c_str(), q.name.c_str());
  TEST_ASSERT_TRUE(!q.enabled && !q.merge_real_pad);
  TEST_ASSERT_EQ_INT(77, q.trigger_button_threshold);
  TEST_ASSERT_EQ_INT(3, (int)q.layers.size());
  const LayerDef &b = q.layers[0];
  TEST_ASSERT_TRUE(std::memcmp(&b.mouse, &p.layers[0].mouse, sizeof(MouseParams)) == 0);
  TEST_ASSERT_TRUE(b.opposing == Opposing::Neutral);
  TEST_ASSERT_EQ_INT(3000, b.mouse_digital_threshold);
  TEST_ASSERT_TRUE(q.layers[1].mouse_inherit && q.layers[1].opposing == Opposing::FirstWins);
  TEST_ASSERT_EQ_INT((int)p.layers[0].bindings.size(), (int)b.bindings.size());
  const BindingDef &w0 = b.bindings[0];
  TEST_ASSERT_EQ_INT(kLShift, w0.modifiers[0]);
  TEST_ASSERT_EQ_INT(kMouseRight, w0.modifiers[1]);
  TEST_ASSERT_TRUE(w0.gesture == Gesture::DoubleTap && w0.mode == Mode::Turbo);
  TEST_ASSERT_EQ_INT(17, w0.turbo_hz);
  TEST_ASSERT_EQ_INT(35, w0.turbo_duty);
  TEST_ASSERT_EQ_INT(150, w0.tap_ms);
  TEST_ASSERT_EQ_INT(500, w0.long_ms);
  TEST_ASSERT_EQ_INT(300, w0.double_ms);
  TEST_ASSERT_EQ_INT(80, w0.pulse_ms);
  KSTREQ("Sprint", w0.label.c_str());
  TEST_ASSERT_EQ_U64(kPadCross | kPadL1, w0.outputs[0].buttons);
  const OutputDef &steer = b.bindings[1].outputs[0];
  TEST_ASSERT_EQ_INT(-16384, steer.value);
  TEST_ASSERT_EQ_INT(120, steer.ramp.rise_ms);
  TEST_ASSERT_EQ_INT(90, steer.ramp.fall_ms);
  const BindingDef &t = b.bindings[2];
  TEST_ASSERT_EQ_INT(2, (int)t.outputs.size());
  TEST_ASSERT_EQ_INT(24576, t.outputs[0].value);
  TEST_ASSERT_EQ_INT(250, t.outputs[0].ramp.curve_x100);
  TEST_ASSERT_EQ_INT(13107, b.bindings[3].outputs[0].value);
  KSTREQ("aim", b.bindings[4].outputs[0].layer.c_str());
  KSTREQ("combo", b.bindings[6].outputs[0].macro.c_str());
  TEST_ASSERT_EQ_INT(2, (int)q.macros.size());
  const MacroDef &c = q.macros[1];
  TEST_ASSERT_TRUE(c.repeat_while_held);
  TEST_ASSERT_EQ_INT(3, (int)c.steps.size());
  TEST_ASSERT_EQ_INT(32767, c.steps[0].axes[kAxisRX]);
  TEST_ASSERT_EQ_INT(-32767, c.steps[0].axes[kAxisLY]);
  TEST_ASSERT_EQ_INT(INT16_MIN, c.steps[0].axes[kAxisLX]);
  TEST_ASSERT_EQ_INT(255, c.steps[0].l2);
  TEST_ASSERT_EQ_INT(12, c.steps[0].r2);
  TEST_ASSERT_EQ_INT(16, c.steps[2].duration_ms);

  /* Compiles the same before and after, byte for byte, whatever the
     destination held before. */
  auto b1 = Blob(0x00), b2 = Blob(0xff);
  TEST_ASSERT_TRUE(Compile(p, b1.get()));
  TEST_ASSERT_TRUE(Compile(q, b2.get()));
  TEST_ASSERT_TRUE(std::memcmp(b1.get(), b2.get(), sizeof(ProfileBlob)) == 0);

  /* The default template survives a round trip too. */
  const Profile d = DefaultProfile();
  const std::string dj = ProfileToJson(d);
  Profile d2;
  TEST_ASSERT_TRUE(ProfileFromJson(dj.c_str(), dj.size(), &d2));
  KSTREQ(dj.c_str(), ProfileToJson(d2).c_str());
  return 0;
}

static int test_json_garbage_rejected(void) {
  const char *bad[] = {"", " ", "{", "}", "[]", "null", "42", "\"profile\"", "true",
                       "{\"schema\":1,", "{'schema':1}", "\xff\xfe{}",
                       "{\"schema\":2,\"layers\":[]}", "{\"schema\":999}"};
  for (const char *b : bad) {
    Profile p;
    std::string err;
    if (ProfileFromJson(b, std::strlen(b), &p, nullptr, &err))
      return onion_test_fail(__FILE__, __LINE__, "accepted \"%s\"", b);
    if (err.empty()) return onion_test_fail(__FILE__, __LINE__, "no error for \"%s\"", b);
  }
  Profile p;
  TEST_ASSERT_TRUE(!ProfileFromJson(nullptr, 0, &p));
  TEST_ASSERT_TRUE(!ProfileFromJson("{}", 0, &p));
  /* Deep nesting: refused or accepted, but no stack overflow. */
  std::string deep(20000, '[');
  deep += std::string(20000, ']');
  std::string nest = "{\"schema\":1,\"layers\":" + deep + "}";
  ProfileFromJson(nest.c_str(), nest.size(), &p);
  /* The length is honoured: bytes past it are not read as JSON. */
  const std::string good = ProfileToJson(DefaultProfile());
  std::string padded = good + "} trailing junk {";
  TEST_ASSERT_TRUE(ProfileFromJson(padded.c_str(), good.size(), &p));
  std::string cut = good.substr(0, good.size() / 2);
  TEST_ASSERT_TRUE(!ProfileFromJson(good.c_str(), cut.size(), &p));
  return 0;
}

static int test_json_bad_bindings_dropped(void) {
  char text[4096];
  std::snprintf(text, sizeof(text), R"({
    "schema": 1, "name": "t", "trigger_button_threshold": 999,
    "layers": [ { "name": "base",
      "mouse": { "mode": "%s", "stick": "right", "sensitivity": 1e308,
                 "deadzone": 0.9, "outer": 0.1, "smoothing_ms": 1, "yx_ratio": -3 },
      "bindings": [
        { "input": "key:W", "turbo_hz": 99, "turbo_duty": 5, "outputs": [ { "out": "pad:cross" } ] },
        { "input": "key:Bogus", "outputs": [ { "out": "pad:circle" } ] },
        { "input": "key:A", "outputs": [ { "out": "pad:nope" } ] },
        { "input": "key:S", "outputs": [ { "out": "axis:lx-", "value": 7, "rise_ms": -5 } ] },
        { "input": "key:D", "outputs": [ { "out": "layer:hold", "layer": "missing" } ] },
        { "input": "key:F", "outputs": [ { "out": "macro", "macro": "missing" } ] },
        { "input": "key:G", "outputs": [] },
        { "input": "key:H" },
        { "input": 42, "outputs": [ { "out": "pad:square" } ] },
        "junk", null, 7,
        { "input": "key:E", "gesture": "wiggle", "outputs": [ { "out": "pad:triangle" } ] },
        { "input": "key:Q", "outputs": [ { "out": "trigger:r2", "value": "full" } ] },
        { "input": "key:R", "modifiers": [ "key:Nope" ], "outputs": [ { "out": "pad:r1" } ] }
      ] },
      "not a layer" ],
    "macros": "not a list",
    "unknown_future_key": { "x": [1, 2, 3] }
  })", MouseModeName(MouseMode::Velocity));
  Profile p;
  Warnings w;
  std::string err;
  if (!ProfileFromJson(text, std::strlen(text), &p, &w, &err))
    return onion_test_fail(__FILE__, __LINE__, "whole profile refused: %s", err.c_str());
  TEST_ASSERT_TRUE(w.size() >= 6);
  TEST_ASSERT_TRUE(!p.layers.empty());
  TEST_ASSERT_EQ_INT(255, p.trigger_button_threshold);
  const LayerDef &l = p.layers[0];
  TEST_ASSERT_TRUE(l.mouse.mode == MouseMode::Velocity);
  TEST_ASSERT_EQ_INT(500, l.mouse.deadzone_x1000);
  TEST_ASSERT_EQ_INT(500, l.mouse.outer_x1000);
  TEST_ASSERT_EQ_INT(4, l.mouse.smoothing_ms);
  TEST_ASSERT_TRUE(l.mouse.sensitivity_x100 > 0);
  /* Y:X 0 is legal (mouse steering on X only); a negative one clamps to it */
  TEST_ASSERT_EQ_INT(0, l.mouse.yx_ratio_x100);

  bool w_ok = false, s_ok = false;
  for (const BindingDef &b : l.bindings) {
    /* half-built bindings stay in the model (the Toolbox is still editing
       them); only Compile skips them */
    if (b.outputs.empty() || b.input == kInputNone)
      continue;
    TEST_ASSERT_TRUE((uint8_t)b.gesture <= (uint8_t)Gesture::Release);
    /* outputs naming a missing layer or macro stay in the model (it may be
       added later) and are skipped by Compile: CheckBlobSane below */
    TEST_ASSERT_TRUE(b.input != 0x0b); /* key:H: no outputs at all */
    TEST_ASSERT_TRUE(b.input != 0x0a); /* key:G: empty outputs */
    TEST_ASSERT_TRUE(b.input != 0x15); /* key:R: unknown modifier must not
                                          silently become a plain R */
    if (b.input == kW) {
      w_ok = b.turbo_hz == 30 && b.turbo_duty == 10 && b.outputs[0].buttons == kPadCross;
    }
    if (b.input == 0x16) {
      s_ok = b.outputs[0].value == -32767 && b.outputs[0].ramp.rise_ms == 0;
    }
  }
  TEST_ASSERT_TRUE(w_ok);
  TEST_ASSERT_TRUE(s_ok);
  auto blob = Blob(0xcc);
  TEST_ASSERT_TRUE(Compile(p, blob.get()));
  return CheckBlobSane(*blob);
}

static int test_json_empty_object_runs(void) {
  /* An object with nothing in it is repaired, and what compiles is runnable. */
  Profile p;
  TEST_ASSERT_TRUE(ProfileFromJson("{}", 2, &p));
  auto blob = Blob(0xee);
  TEST_ASSERT_TRUE(Compile(p, blob.get()));
  if (CheckBlobSane(*blob)) return 1;
  const char *empty_layers = "{\"schema\":1,\"layers\":[]}";
  TEST_ASSERT_TRUE(ProfileFromJson(empty_layers, std::strlen(empty_layers), &p));
  TEST_ASSERT_TRUE(Compile(p, blob.get()));
  return CheckBlobSane(*blob);
}

/* ---- validation and Compile ------------------------------------------- */

static int test_validate_clamps(void) {
  Profile p;
  p.trigger_button_threshold = 4000;
  LayerDef l;
  l.name = "base";
  l.mouse.deadzone_x1000 = 1000;
  l.mouse.outer_x1000 = 0;
  l.mouse.smoothing_ms = 0;
  l.mouse.stick = 9;
  l.mouse.mode = MouseMode(77);
  l.mouse.deadzone_shape = DeadzoneShape(9);
  l.opposing = Opposing(9);
  BindingDef a = Bind(kA, Btn(kPadCross));
  a.turbo_hz = 0;
  a.turbo_duty = 0;
  a.gesture = Gesture(40);
  a.mode = Mode(40);
  BindingDef b = Bind(kE, Btn(kPadCircle));
  b.turbo_hz = 255;
  b.turbo_duty = 255;
  OutputDef ax;
  ax.kind = OutputKind::AxisDirection;
  ax.axis = kAxisRY;
  ax.value = INT16_MIN;
  ax.ramp.curve_x100 = 0;
  BindingDef c = Bind(kQ, ax);
  OutputDef tr;
  tr.kind = OutputKind::Trigger;
  tr.trigger = kTriggerL2;
  tr.value = -100;
  BindingDef d = Bind(kM, tr);
  OutputDef sc;
  sc.kind = OutputKind::StickScale;
  sc.axis = 1;
  sc.value = -5;
  BindingDef e = Bind(kW, sc);
  BindingDef many = Bind(kF12, Btn(kPadL1));
  for (int i = 0; i < 6; ++i) many.outputs.push_back(Btn(kPadR1));
  l.bindings = {a, b, c, d, e, many};
  p.layers = {l};

  Warnings w;
  Validate(p, &w);
  TEST_ASSERT_TRUE(!w.empty());
  TEST_ASSERT_TRUE(p.trigger_button_threshold <= 255);
  const LayerDef &v = p.layers[0];
  TEST_ASSERT_EQ_INT(500, v.mouse.deadzone_x1000);
  TEST_ASSERT_EQ_INT(500, v.mouse.outer_x1000);
  TEST_ASSERT_EQ_INT(4, v.mouse.smoothing_ms);
  TEST_ASSERT_TRUE(v.mouse.stick <= 1);
  TEST_ASSERT_TRUE((uint8_t)v.mouse.mode <= (uint8_t)MouseMode::Position);
  TEST_ASSERT_TRUE((uint8_t)v.mouse.deadzone_shape <= 1);
  TEST_ASSERT_TRUE((uint8_t)v.opposing <= (uint8_t)Opposing::FirstWins);
  for (const BindingDef &x : v.bindings) {
    TEST_ASSERT_TRUE(x.turbo_hz >= 1 && x.turbo_hz <= 30);
    TEST_ASSERT_TRUE(x.turbo_duty >= 10 && x.turbo_duty <= 90);
    TEST_ASSERT_TRUE((uint8_t)x.gesture <= (uint8_t)Gesture::Release);
    TEST_ASSERT_TRUE((uint8_t)x.mode <= (uint8_t)Mode::Pulse);
    /* the model keeps extra outputs; Compile takes the first four */
    for (const OutputDef &o : x.outputs) {
      if (o.kind == OutputKind::AxisDirection) {
        TEST_ASSERT_EQ_INT(-32767, o.value);
        TEST_ASSERT_TRUE(o.ramp.curve_x100 > 0);
      }
      if (o.kind == OutputKind::Trigger || o.kind == OutputKind::StickScale)
        TEST_ASSERT_TRUE(o.value >= 0);
    }
  }
  /* Validating twice changes nothing more. */
  Profile again = p;
  Warnings w2;
  Validate(again, &w2);
  TEST_ASSERT_TRUE(w2.empty());
  KSTREQ(ProfileToJson(p).c_str(), ProfileToJson(again).c_str());
  return 0;
}

static int test_compile_layout(void) {
  Profile p = RichProfile();
  p.layers[0].bindings.push_back(Bind(kPause, Btn(kPadCircle))); /* suspend key */
  auto blob = Blob(0xa5);
  Warnings w;
  TEST_ASSERT_TRUE(Compile(p, blob.get(), kPause, &w));
  if (CheckBlobSane(*blob)) return 1;
  const ProfileBlob &b = *blob;
  TEST_ASSERT_EQ_INT(0, b.enabled);
  TEST_ASSERT_EQ_INT(0, b.merge_real_pad);
  TEST_ASSERT_EQ_INT(77, b.trigger_button_threshold);
  TEST_ASSERT_EQ_INT(kPause, b.suspend_key);
  TEST_ASSERT_EQ_INT(3, b.layer_count);
  KSTREQ("aim", b.layers[1].name);
  TEST_ASSERT_EQ_INT(1, b.layers[1].mouse_inherit);
  TEST_ASSERT_TRUE(b.layers[2].mouse.mode == MouseMode::Position);
  TEST_ASSERT_EQ_INT(220, b.layers[0].mouse.deadzone_x1000);
  TEST_ASSERT_TRUE(b.layers[0].opposing == Opposing::Neutral);
  TEST_ASSERT_TRUE(!HasBinding(b, kPause, 0));
  TEST_ASSERT_EQ_INT((int)p.layers[0].bindings.size() - 1 + 1, b.binding_count);

  const Binding *x = nullptr;
  TEST_ASSERT_TRUE(HasBinding(b, kQ, 0, &x));
  TEST_ASSERT_TRUE(x->outputs[0].kind == OutputKind::LayerHold);
  TEST_ASSERT_EQ_INT(1, x->outputs[0].layer);
  TEST_ASSERT_TRUE(HasBinding(b, 0x2b, 0, &x));
  TEST_ASSERT_EQ_INT(2, x->outputs[0].layer);
  TEST_ASSERT_TRUE(HasBinding(b, kW, 1, &x)); /* the aim layer's W */
  TEST_ASSERT_TRUE(x->gesture == Gesture::Tap);
  TEST_ASSERT_TRUE(HasBinding(b, kW, 0, &x));
  TEST_ASSERT_EQ_INT(kLShift, x->modifiers[0]);
  TEST_ASSERT_EQ_INT(17, x->turbo_hz);
  TEST_ASSERT_EQ_INT(80, x->pulse_ms);
  TEST_ASSERT_TRUE(HasBinding(b, kE, 0, &x));
  TEST_ASSERT_EQ_INT(2, x->output_count);
  TEST_ASSERT_EQ_INT(24576, x->outputs[0].value);
  TEST_ASSERT_EQ_INT(300, x->outputs[0].ramp.rise_ms);

  /* Macros: names to indices, steps packed back to back. */
  TEST_ASSERT_EQ_INT(2, b.macro_count);
  TEST_ASSERT_EQ_INT(4, b.macro_step_count);
  TEST_ASSERT_TRUE(HasBinding(b, kM, 0, &x));
  const Macro &mc = b.macros[x->outputs[0].macro];
  TEST_ASSERT_EQ_INT(3, mc.step_count);
  TEST_ASSERT_EQ_INT(1, mc.repeat_while_held);
  TEST_ASSERT_EQ_U64(kPadL3 | kPadR3, b.macro_steps[mc.first_step].buttons);
  TEST_ASSERT_EQ_INT(80, b.macro_steps[mc.first_step].duration_ms);
  TEST_ASSERT_EQ_U64(kPadCircle, b.macro_steps[mc.first_step + 2].buttons);

  /* Another suspend key: Pause is ordinary, F12 is now reserved. */
  TEST_ASSERT_TRUE(Compile(p, blob.get(), kF12, &w));
  TEST_ASSERT_EQ_INT(kF12, blob->suspend_key);
  TEST_ASSERT_TRUE(HasBinding(*blob, kPause, 0));
  TEST_ASSERT_TRUE(!HasBinding(*blob, kF12, 0));
  TEST_ASSERT_TRUE(!Compile(p, nullptr));
  return 0;
}

static int test_compile_names_not_positions(void) {
  /* Deleting a layer or macro in the editor must not shift a reference onto
     another one. */
  Profile p;
  LayerDef base, a, b;
  base.name = "base";
  a.name = "a";
  b.name = "b";
  base.bindings.push_back(Bind(kQ, Named(OutputKind::LayerHold, "b")));
  base.bindings.push_back(Bind(kE, Named(OutputKind::LayerHold, "a")));
  base.bindings.push_back(Bind(kM, Named(OutputKind::Macro, "second")));
  base.bindings.push_back(Bind(kA, Named(OutputKind::Macro, "first")));
  p.layers = {base, a, b};
  MacroDef m1, m2;
  m1.name = "first";
  m1.steps = {Step(kPadCross, 10)};
  m2.name = "second";
  m2.steps = {Step(kPadCircle, 10), Step(kPadSquare, 10)};
  p.macros = {m1, m2};
  p.layers.erase(p.layers.begin() + 1);
  p.macros.erase(p.macros.begin());
  auto blob = Blob(0);
  Warnings w;
  TEST_ASSERT_TRUE(Compile(p, blob.get(), kPause, &w));
  if (CheckBlobSane(*blob)) return 1;
  const Binding *x = nullptr;
  TEST_ASSERT_TRUE(HasBinding(*blob, kQ, 0, &x));
  TEST_ASSERT_EQ_INT(1, x->outputs[0].layer);
  TEST_ASSERT_TRUE(!HasBinding(*blob, kE, 0)); /* its layer is gone */
  TEST_ASSERT_TRUE(HasBinding(*blob, kM, 0, &x));
  TEST_ASSERT_EQ_U64(kPadCircle, blob->macro_steps[blob->macros[x->outputs[0].macro].first_step].buttons);
  TEST_ASSERT_TRUE(!HasBinding(*blob, kA, 0)); /* its macro is gone */
  TEST_ASSERT_TRUE(w.size() >= 2);
  return 0;
}

static int test_compile_limits(void) {
  Profile p;
  for (int l = 0; l < 12; ++l) {
    LayerDef d;
    char n[16];
    std::snprintf(n, sizeof(n), "layer%d", l);
    d.name = n;
    for (int k = 0; k < 40; ++k) d.bindings.push_back(Bind(Input(0x04 + k), Btn(kPadCross)));
    p.layers.push_back(d);
  }
  for (int m = 0; m < 40; ++m) {
    MacroDef d;
    d.name = "m" + std::to_string(m);
    for (int s = 0; s < 20; ++s) d.steps.push_back(Step(kPadCircle, 10));
    p.macros.push_back(d);
    p.layers[0].bindings.push_back(Bind(Input(0x50 + m), Named(OutputKind::Macro, d.name.c_str())));
  }
  /* An over-long layer name. */
  p.layers[1].name = std::string(100, 'x');
  auto blob = Blob(0x5a);
  Warnings w;
  TEST_ASSERT_TRUE(Compile(p, blob.get(), kPause, &w));
  TEST_ASSERT_TRUE(!w.empty());
  if (CheckBlobSane(*blob)) return 1;
  TEST_ASSERT_EQ_INT(kMaxLayers, blob->layer_count);
  TEST_ASSERT_EQ_INT(kMaxBindings, blob->binding_count);
  TEST_ASSERT_TRUE(blob->macro_count >= 1);

  /* the model keeps everything; the limits are the blob's */
  TEST_ASSERT_TRUE(blob->macro_count <= kMaxMacros);
  TEST_ASSERT_TRUE(blob->macro_step_count <= kMaxMacroSteps);
  return 0;
}

static int test_default_profile(void) {
  const Profile d = DefaultProfile();
  TEST_ASSERT_TRUE(!d.layers.empty());
  Profile v = d;
  Warnings w;
  Validate(v, &w);
  if (!w.empty())
    return onion_test_fail(__FILE__, __LINE__, "default warns: %s", w[0].c_str());
  auto blob = Blob(0x11);
  TEST_ASSERT_TRUE(Compile(d, blob.get()));
  if (CheckBlobSane(*blob)) return 1;
  TEST_ASSERT_TRUE(blob->binding_count > 0);
  TEST_ASSERT_EQ_INT(1, blob->enabled);
  TEST_ASSERT_EQ_INT(kDefaultSuspendKey, blob->suspend_key);
  return 0;
}

/* ---- store ------------------------------------------------------------ */

static int test_store_paths_and_files(void) {
  TEST_ASSERT_TRUE(IsValidTitleId("PPSA04264"));
  TEST_ASSERT_TRUE(IsValidTitleId("CUSA00411"));
  const char *bad[] = {"", "../x", "PPSA/0426", "PPSA04264.json", "ppsa04264 ",
                       "AAAAAAAAAAAAAAAAA", "..", "PPSA\\1"};
  for (const char *b : bad) {
    if (IsValidTitleId(b)) return onion_test_fail(__FILE__, __LINE__, "\"%s\" valid", b);
    if (!TitleProfilePath("/r", b).empty())
      return onion_test_fail(__FILE__, __LINE__, "\"%s\" got a path", b);
  }
  const std::string root = TempDir();
  TEST_ASSERT_TRUE(!root.empty());
  const std::string kbm = root + "/kbm";

  /* Nothing saved: the generic default. */
  Profile r = ResolveProfile(kbm, "PPSA04264");
  KSTREQ(ProfileToJson(DefaultProfile()).c_str(), ProfileToJson(r).c_str());
  TEST_ASSERT_TRUE(ListTitleProfiles(kbm).empty());

  Profile rich = RichProfile();
  const std::string tp = TitleProfilePath(kbm, "PPSA04264");
  TEST_ASSERT_TRUE(SaveProfile(tp, rich));
  Profile back;
  TEST_ASSERT_TRUE(LoadProfile(tp, &back));
  KSTREQ(ProfileToJson(rich).c_str(), ProfileToJson(back).c_str());

  Profile def = DefaultProfile();
  def.name = "my default";
  TEST_ASSERT_TRUE(SaveProfile(DefaultProfilePath(kbm), def));
  const Profile rp1 = ResolveProfile(kbm, "CUSA00411");
  KSTREQ("my default", rp1.name.c_str());
  const Profile rp2 = ResolveProfile(kbm, "PPSA04264");
  KSTREQ(rich.name.c_str(), rp2.name.c_str());
  TEST_ASSERT_TRUE(SaveProfile(TitleProfilePath(kbm, "CUSA00001"), def));
  auto list = ListTitleProfiles(kbm);
  TEST_ASSERT_EQ_INT(2, (int)list.size());
  KSTREQ("CUSA00001", list[0].c_str());
  KSTREQ("PPSA04264", list[1].c_str());

  /* A broken title file falls back instead of failing the game. */
  FILE *f = std::fopen(TitleProfilePath(kbm, "BROKEN1").c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  std::fputs("{ not json", f);
  std::fclose(f);
  const Profile rp3 = ResolveProfile(kbm, "BROKEN1");
  KSTREQ("my default", rp3.name.c_str());

  /* Oversized file refused. */
  const std::string big = TitleProfilePath(kbm, "BIG1");
  f = std::fopen(big.c_str(), "w");
  std::string j = ProfileToJson(def);
  j.insert(1, "\"pad\":\"" + std::string(kMaxProfileFileSize, 'x') + "\",");
  std::fputs(j.c_str(), f);
  std::fclose(f);
  TEST_ASSERT_TRUE(!LoadProfile(big, &back));

  TEST_ASSERT_TRUE(DeleteProfile(tp));
  const Profile rp4 = ResolveProfile(kbm, "PPSA04264");
  KSTREQ("my default", rp4.name.c_str());

  /* Settings. */
  Settings s = LoadSettings(kbm);
  TEST_ASSERT_TRUE(!s.enabled);
  TEST_ASSERT_EQ_INT(kDefaultSuspendKey, s.suspend_key);
  s.enabled = true;
  s.suspend_key = kF12;
  TEST_ASSERT_TRUE(SaveSettings(kbm, s));
  Settings s2 = LoadSettings(kbm);
  TEST_ASSERT_TRUE(s2.enabled);
  TEST_ASSERT_EQ_INT(kF12, s2.suspend_key);
  Settings s3;
  TEST_ASSERT_TRUE(!SettingsFromJson("[", 1, &s3));
  const std::string sj = SettingsToJson(s);
  TEST_ASSERT_TRUE(SettingsFromJson(sj.c_str(), sj.size(), &s3));
  TEST_ASSERT_TRUE(s3.enabled && s3.suspend_key == kF12);

  std::string cmd = "rm -rf '" + root + "'";
  (void)std::system(cmd.c_str());
  return 0;
}

extern "C" int test_kbm_model_suite(void) {
  int failures = 0;
  failures += onion_test_run("kbm_names_every_input", test_names_every_input);
  failures += onion_test_run("kbm_names_spellings", test_names_spellings);
  failures += onion_test_run("kbm_names_buttons", test_names_buttons);
  failures += onion_test_run("kbm_names_outputs", test_names_outputs);
  failures += onion_test_run("kbm_names_enums", test_names_enums);
  failures += onion_test_run("kbm_json_round_trip", test_json_round_trip);
  failures += onion_test_run("kbm_json_garbage_rejected", test_json_garbage_rejected);
  failures += onion_test_run("kbm_json_bad_bindings_dropped", test_json_bad_bindings_dropped);
  failures += onion_test_run("kbm_json_empty_object_runs", test_json_empty_object_runs);
  failures += onion_test_run("kbm_validate_clamps", test_validate_clamps);
  failures += onion_test_run("kbm_compile_layout", test_compile_layout);
  failures += onion_test_run("kbm_compile_names_not_positions", test_compile_names_not_positions);
  failures += onion_test_run("kbm_compile_limits", test_compile_limits);
  failures += onion_test_run("kbm_default_profile", test_default_profile);
  failures += onion_test_run("kbm_store_paths_files", test_store_paths_and_files);
  return failures;
}
