/* Host tests for the KB+M engine (onion::kbm::Engine), written from the
 * contract in kbm_types.hpp: blobs are built by hand here, not through
 * Compile(), so a model bug cannot hide an engine bug. */
#include "test_harness.h"

#include <onion/kbm_engine.hpp>
#include <onion/kbm_types.hpp>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace onion::kbm;

namespace {

/* HID usages */
constexpr Input kA = 0x04, kD = 0x07, kE = 0x08, kG = 0x0a, kM = 0x10,
                kQ = 0x14, kR = 0x15, kS = 0x16, kT = 0x17, kW = 0x1a,
                kF12 = 0x45, kPause = 0x48, kLCtrl = 0xe0, kLShift = 0xe1,
                kLAlt = 0xe2;

/* Past 2^32 us: a clock truncated to 32 bits shows up as nonsense. */
constexpr uint64_t kBaseUs = 5000000000ull;

#define KASSERT_RANGE(lo, hi, actual)                                          \
  do {                                                                         \
    long long v__ = (long long)(actual);                                       \
    if (v__ < (long long)(lo) || v__ > (long long)(hi)) {                      \
      return onion_test_fail(__FILE__, __LINE__, "%s = %lld, want %lld..%lld", \
                             #actual, v__, (long long)(lo), (long long)(hi));  \
    }                                                                          \
  } while (0)

Output OButton(uint32_t mask) {
  Output o{};
  o.kind = OutputKind::Button;
  o.buttons = mask;
  return o;
}
Output OAxis(uint8_t axis, int16_t value, Ramp ramp = {0, 0, 100, 0}) {
  Output o{};
  o.kind = OutputKind::AxisDirection;
  o.axis = axis;
  o.value = value;
  o.ramp = ramp;
  return o;
}
Output OTrigger(uint8_t trigger, int16_t value, Ramp ramp = {0, 0, 100, 0}) {
  Output o{};
  o.kind = OutputKind::Trigger;
  o.trigger = trigger;
  o.value = value;
  o.ramp = ramp;
  return o;
}
Output OKind(OutputKind kind) {
  Output o{};
  o.kind = kind;
  return o;
}
Output OLayer(OutputKind kind, uint8_t layer) {
  Output o = OKind(kind);
  o.layer = layer;
  return o;
}
Output OMacro(uint8_t macro) {
  Output o = OKind(OutputKind::Macro);
  o.macro = macro;
  return o;
}
Output OScale(uint8_t stick, int16_t value) {
  Output o = OKind(OutputKind::StickScale);
  o.axis = stick;
  o.value = value;
  return o;
}

MouseParams VelocityMouse() {
  MouseParams m{};
  m.mode = MouseMode::Velocity;
  m.stick = 1;
  m.deadzone_shape = DeadzoneShape::Circular;
  m.sensitivity_x100 = 100;
  m.yx_ratio_x100 = 100;
  m.exponent_x100 = 100;
  m.deadzone_x1000 = 0;
  m.outer_x1000 = 1000;
  m.smoothing_ms = 12;
  m.boost_x100 = 100;
  m.position_range = 1000;
  m.spring_per_s_x10 = 0;
  return m;
}

struct Rig {
  ProfileBlob blob{};
  Engine eng;
  InputFrame f{};
  PadState out{};
  uint64_t now = kBaseUs;

  Rig() {
    blob.magic = kBlobMagic;
    blob.version = kBlobVersion;
    blob.size = sizeof(ProfileBlob);
    blob.enabled = 1;
    blob.layer_count = 1;
    blob.suspend_key = kPause;
    blob.trigger_button_threshold = 30;
    blob.merge_real_pad = 1;
    for (int i = 0; i < kMaxLayers; ++i) {
      std::snprintf(blob.layers[i].name, sizeof(blob.layers[i].name), "l%d", i);
      blob.layers[i].opposing = Opposing::LastWins;
      blob.layers[i].mouse_digital_threshold = 1000;
      blob.layers[i].mouse_inherit = i ? 1 : 0;
    }
  }

  Binding &add(Input in, Output o, uint8_t layer = 0) {
    Binding &b = blob.bindings[blob.binding_count++];
    b = Binding{};
    b.input = in;
    b.layer = layer;
    b.gesture = Gesture::Press;
    b.mode = Mode::Hold;
    b.turbo_hz = 10;
    b.turbo_duty = 50;
    b.tap_ms = 200;
    b.long_ms = 400;
    b.double_ms = 250;
    b.pulse_ms = 60;
    b.output_count = 1;
    b.outputs[0] = o;
    if (layer + 1 > blob.layer_count) blob.layer_count = layer + 1;
    return b;
  }
  Binding &btn(Input in, uint32_t mask, uint8_t layer = 0) {
    return add(in, OButton(mask), layer);
  }
  /* Keys held when a profile arrives count only after a new press, so the
     first read after SetProfile is an idle one. */
  void apply() {
    eng.SetProfile(&blob);
    InputFrame idle{};
    now += 1000;
    idle.now_us = now;
    eng.Tick(idle, &out);
  }

  void press(Input in) { f.down[in >> 3] |= uint8_t(1u << (in & 7)); }
  void release(Input in) { f.down[in >> 3] &= uint8_t(~(1u << (in & 7))); }

  /* Advance the clock by dt_ms and evaluate one game read. Motion and wheel
     notches are per frame, so they are cleared after use. */
  const PadState &tick(uint32_t dt_ms = 1) {
    now += uint64_t(dt_ms) * 1000;
    f.now_us = now;
    std::memset(&out, 0xa5, sizeof(out)); /* the engine must fill all of it */
    eng.Tick(f, &out);
    f.wheel_v = f.wheel_h = 0;
    f.mouse_dx = f.mouse_dy = 0;
    return out;
  }
  void run(uint32_t ms, uint32_t step = 1) {
    for (uint32_t t = 0; t < ms; t += step) tick(step);
  }
  /* ms the mask was fully set over the next `ms`, at 1 ms reads. */
  int on_ms(uint32_t mask, uint32_t ms) {
    int on = 0;
    for (uint32_t t = 0; t < ms; ++t)
      if ((tick(1).buttons & mask) == mask) ++on;
    return on;
  }
  int rising_edges(uint32_t mask, uint32_t ms, bool *was_on = nullptr) {
    bool prev = was_on ? *was_on : false;
    int edges = 0;
    for (uint32_t t = 0; t < ms; ++t) {
      bool on = (tick(1).buttons & mask) != 0;
      if (on && !prev) ++edges;
      prev = on;
    }
    if (was_on) *was_on = prev;
    return edges;
  }
};

std::unique_ptr<Rig> NewRig() { return std::make_unique<Rig>(); }

bool Idle(const PadState &s) {
  const bool idle = s.active == 0 && s.buttons == 0 && s.lx == 128 &&
                    s.ly == 128 && s.rx == 128 && s.ry == 128 && s.l2 == 0 &&
                    s.r2 == 0;
  if (!idle)
    std::fprintf(stderr, "  not idle: active %d buttons 0x%x L %d,%d R %d,%d T %d,%d\n",
                 s.active, s.buttons, s.lx, s.ly, s.rx, s.ry, s.l2, s.r2);
  return idle;
}

double Defl(uint8_t b) { return (double(b) - 128.0) / 127.0; }

} // namespace

/* ---- pass-through ----------------------------------------------------- */

static int test_idle_inactive(void) {
  auto r = NewRig();
  /* No profile at all. */
  TEST_ASSERT_TRUE(Idle(r->tick()));
  r->btn(kW, kPadCross);
  r->blob.layers[0].mouse = VelocityMouse();
  r->apply();
  for (int i = 0; i < 50; ++i) TEST_ASSERT_TRUE(Idle(r->tick(4)));
  /* After a press is over, nothing of ours stays behind. */
  r->press(kW);
  TEST_ASSERT_EQ_INT(1, r->tick().active);
  r->release(kW);
  r->run(500);
  TEST_ASSERT_TRUE(Idle(r->tick()));
  return 0;
}

static int test_disabled_or_bad_blob_inactive(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross);
  r->blob.enabled = 0;
  r->apply();
  r->press(kW);
  TEST_ASSERT_TRUE(Idle(r->tick()));

  auto m = NewRig();
  m->btn(kW, kPadCross);
  m->blob.magic = 0x12345678;
  m->apply();
  m->press(kW);
  TEST_ASSERT_TRUE(Idle(m->tick()));

  auto v = NewRig();
  v->btn(kW, kPadCross);
  v->blob.version = kBlobVersion + 1;
  v->apply();
  v->press(kW);
  TEST_ASSERT_TRUE(Idle(v->tick()));

  /* Valid, then cleared while a key is down: nothing may stay pressed. */
  auto n = NewRig();
  n->btn(kW, kPadCross);
  n->apply();
  n->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, n->tick().buttons);
  n->eng.SetProfile(nullptr);
  TEST_ASSERT_TRUE(Idle(n->tick()));
  return 0;
}

static int test_corrupt_blob_no_crash(void) {
  auto r = NewRig();
  r->blob.layer_count = 200;
  r->blob.macro_count = 255;
  r->blob.binding_count = kMaxBindings;
  r->blob.macro_step_count = 0xffff;
  for (int i = 0; i < kMaxBindings; ++i) {
    Binding &b = r->blob.bindings[i];
    b.input = Input(i * 7 + 0x8000 * (i & 1)); /* half out of range */
    b.modifiers[0] = Input(0xffff - i);
    b.layer = uint8_t(i);
    b.gesture = Gesture(i % 9);
    b.mode = Mode(i % 7);
    b.output_count = uint8_t(i % 9);
    b.turbo_hz = uint8_t(i);
    b.turbo_duty = uint8_t(i * 3);
    b.pulse_ms = uint16_t(i % 3);
    for (int k = 0; k < kMaxOutputsPerBinding; ++k) {
      Output &o = b.outputs[k];
      o.kind = OutputKind((i + k) % 12);
      o.axis = uint8_t(i + k);
      o.trigger = uint8_t(i * k);
      o.layer = uint8_t(255 - i);
      o.macro = uint8_t(i);
      o.value = int16_t(i * 300 - 32768);
    }
  }
  for (int i = 0; i < kMaxMacros; ++i) {
    r->blob.macros[i].first_step = uint16_t(500 + i);
    r->blob.macros[i].step_count = 400;
    r->blob.macros[i].repeat_while_held = 1;
  }
  for (int i = 0; i < kMaxLayers; ++i) {
    r->blob.layers[i].mouse = VelocityMouse();
    r->blob.layers[i].mouse.stick = uint8_t(i * 3);
    r->blob.layers[i].mouse.mode = MouseMode(i % 4);
    r->blob.layers[i].mouse.smoothing_ms = 0;
    r->blob.layers[i].mouse.outer_x1000 = 0;
    r->blob.layers[i].mouse.position_range = 0;
    r->blob.layers[i].opposing = Opposing(i % 5);
  }
  r->apply();
  std::memset(r->f.down, 0xff, sizeof(r->f.down));
  r->f.mouse_dx = INT32_MIN;
  r->f.mouse_dy = INT32_MAX;
  r->f.wheel_v = INT16_MIN;
  r->f.wheel_h = INT16_MAX;
  r->tick(0); /* same timestamp twice: dt 0 */
  r->tick(0);
  for (int i = 0; i < 200; ++i) {
    r->f.mouse_dx = (i & 1) ? INT32_MAX : INT32_MIN;
    r->f.wheel_v = 3;
    r->tick(3);
  }
  /* Clock going backwards (another thread's stale stamp). */
  r->now -= 100000;
  r->tick(1);
  r->eng.Reset();
  r->tick(1);
  return 0; /* reaching here without a crash is the pass */
}

/* ---- buttons and chords ----------------------------------------------- */

static int test_key_gives_button(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross);
  r->btn(kE, kPadCross);
  r->btn(kMouseLeft, kPadR1);
  r->add(kQ, OButton(kPadSquare)).outputs[1] = OTrigger(kTriggerR2, 32767);
  r->blob.bindings[3].output_count = 2;
  r->apply();

  r->press(kW);
  const PadState &s = r->tick();
  TEST_ASSERT_EQ_U64(kPadCross, s.buttons);
  TEST_ASSERT_EQ_INT(1, s.active);
  TEST_ASSERT_EQ_INT(128, s.lx);
  TEST_ASSERT_EQ_INT(0, s.r2);
  /* Two inputs on one button: releasing one keeps it. */
  r->press(kE);
  r->tick();
  r->release(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->release(kE);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);

  r->press(kMouseLeft);
  TEST_ASSERT_EQ_U64(kPadR1, r->tick().buttons);
  r->release(kMouseLeft);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);

  /* Several outputs on one binding. */
  r->press(kQ);
  r->tick();
  TEST_ASSERT_EQ_U64(kPadSquare | kPadR2, r->out.buttons);
  TEST_ASSERT_EQ_INT(255, r->out.r2);
  return 0;
}

static int test_chord_priority(void) {
  auto r = NewRig();
  r->btn(kW, kPadCircle);
  Binding &one = r->btn(kW, kPadCross);
  one.modifiers[0] = kLShift;
  Binding &two = r->btn(kW, kPadSquare);
  two.modifiers[0] = kLShift;
  two.modifiers[1] = kLCtrl;
  r->apply();

  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  r->release(kW);
  r->tick();

  r->press(kLShift);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons); /* a modifier alone is nothing */
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons); /* more modifiers win */
  r->release(kW);
  r->tick();

  r->press(kLCtrl);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadSquare, r->tick().buttons);
  r->release(kW);
  r->release(kLCtrl);
  r->release(kLShift);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  return 0;
}

static int test_chord_order_independent_of_binding_order(void) {
  /* Same as above with the chord listed first: priority must not depend on
     the order bindings sit in the blob. */
  auto r = NewRig();
  Binding &one = r->btn(kW, kPadCross);
  one.modifiers[0] = kLShift;
  r->btn(kW, kPadCircle);
  r->apply();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  r->release(kW);
  r->tick();
  r->press(kLShift);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  return 0;
}

/* ---- layers -------------------------------------------------------------- */

static int test_layer_hold_and_fallthrough(void) {
  auto r = NewRig();
  r->add(kQ, OLayer(OutputKind::LayerHold, 1));
  r->btn(kW, kPadCircle);
  r->btn(kE, kPadTriangle);
  r->btn(kW, kPadCross, 1);
  r->apply();

  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  r->release(kW);
  r->tick();

  r->press(kQ);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons); /* a layer key outputs nothing */
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->press(kE); /* not bound in layer 1: falls through to layer 0 */
  TEST_ASSERT_EQ_U64(kPadCross | kPadTriangle, r->tick().buttons);
  r->release(kW);
  r->release(kE);
  r->tick();
  r->release(kQ);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  return 0;
}

static int test_layer_none_blocks(void) {
  auto r = NewRig();
  r->add(kQ, OLayer(OutputKind::LayerHold, 1));
  r->btn(kR, kPadSquare);
  r->add(kR, OKind(OutputKind::None), 1);
  r->apply();
  r->press(kQ);
  r->tick();
  r->press(kR);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->release(kR);
  r->release(kQ);
  r->tick();
  r->press(kR);
  TEST_ASSERT_EQ_U64(kPadSquare, r->tick().buttons);
  return 0;
}

static int test_layer_latched_until_release(void) {
  auto r = NewRig();
  r->add(kQ, OLayer(OutputKind::LayerHold, 1));
  r->btn(kW, kPadCircle);
  r->btn(kW, kPadCross, 1);
  r->apply();

  /* Pressed in layer 1, the layer goes away: W stays Cross until released. */
  r->press(kQ);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->release(kQ);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->release(kW);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);

  /* Pressed in layer 0, the layer comes: W stays Circle. */
  r->press(kQ);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  r->release(kW);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  return 0;
}

static int test_layer_toggle(void) {
  auto r = NewRig();
  r->add(kT, OLayer(OutputKind::LayerToggle, 1));
  r->btn(kW, kPadCircle);
  r->btn(kW, kPadCross, 1);
  r->apply();
  r->press(kT);
  r->run(30);
  r->release(kT);
  r->run(30);
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->release(kW);
  r->run(10);
  /* Holding the toggle key does not flip it again. */
  r->press(kT);
  r->run(500);
  r->release(kT);
  r->run(10);
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  return 0;
}

static int test_layer_toggle_key_latched(void) {
  /* The toggle key, bound in layer 1 too, must toggle back off (G5 F-key
     example): its press resolves in the layer it was pressed in. */
  auto r = NewRig();
  r->add(kT, OLayer(OutputKind::LayerToggle, 1));
  r->add(kT, OLayer(OutputKind::LayerToggle, 1), 1);
  r->btn(kW, kPadCircle);
  r->btn(kW, kPadCross, 1);
  r->apply();
  for (int round = 0; round < 3; ++round) {
    r->press(kT);
    r->run(20);
    r->release(kT);
    r->run(20);
    r->press(kW);
    TEST_ASSERT_EQ_U64(round % 2 == 0 ? kPadCross : kPadCircle, r->tick().buttons);
    r->release(kW);
    r->run(5);
  }
  return 0;
}

/* ---- gestures -------------------------------------------------------- */

static int test_gesture_tap(void) {
  auto r = NewRig();
  Binding &b = r->btn(kW, kPadCross);
  b.gesture = Gesture::Tap;
  r->apply();

  r->press(kW);
  for (int i = 0; i < 190; ++i) TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->release(kW);
  KASSERT_RANGE(60, 75, r->on_ms(kPadCross, 300));

  /* Just past tap_ms: not a tap. */
  r->press(kW);
  r->run(210);
  r->release(kW);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 300));

  /* One-read tap still gives a full pulse. */
  r->press(kW);
  r->tick();
  r->release(kW);
  KASSERT_RANGE(60, 75, r->on_ms(kPadCross, 300));
  return 0;
}

static int test_gesture_long_press(void) {
  auto r = NewRig();
  Binding &b = r->btn(kW, kPadCross);
  b.gesture = Gesture::LongPress;
  r->apply();
  r->press(kW);
  for (int i = 0; i < 395; ++i) TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->run(10);
  for (int i = 0; i < 200; ++i) TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
  r->release(kW);
  r->tick();
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 300));

  /* A short press never fires. */
  r->press(kW);
  r->run(300);
  r->release(kW);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 500));
  return 0;
}

static int test_gesture_tap_and_long_exclusive(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross).gesture = Gesture::Tap;
  r->btn(kW, kPadCircle).gesture = Gesture::LongPress;
  r->apply();

  r->press(kW);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQ_U64(0, r->tick().buttons);
  r->release(kW);
  int cross = 0, circle = 0;
  for (int i = 0; i < 600; ++i) {
    uint32_t b = r->tick().buttons;
    cross += (b & kPadCross) != 0;
    circle += (b & kPadCircle) != 0;
  }
  KASSERT_RANGE(60, 75, cross);
  TEST_ASSERT_EQ_INT(0, circle);

  r->press(kW);
  cross = circle = 0;
  for (int i = 0; i < 600; ++i) {
    uint32_t b = r->tick().buttons;
    cross += (b & kPadCross) != 0;
    circle += (b & kPadCircle) != 0;
  }
  r->release(kW);
  for (int i = 0; i < 600; ++i) {
    uint32_t b = r->tick().buttons;
    cross += (b & kPadCross) != 0;
    circle += (b & kPadCircle) != 0;
  }
  TEST_ASSERT_EQ_INT(0, cross);
  KASSERT_RANGE(190, 210, circle);
  return 0;
}

static int test_gesture_double_tap(void) {
  auto r = NewRig();
  r->btn(kW, kPadTriangle).gesture = Gesture::DoubleTap;
  r->apply();

  /* First press alone: nothing. */
  r->press(kW);
  r->run(50);
  r->release(kW);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadTriangle, 100));
  /* Second press inside double_ms: fires while held. */
  r->press(kW);
  r->run(5);
  for (int i = 0; i < 300; ++i) TEST_ASSERT_EQ_U64(kPadTriangle, r->tick().buttons);
  r->release(kW);
  r->run(2);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadTriangle, 600));

  /* Second press too late. */
  r->press(kW);
  r->run(50);
  r->release(kW);
  r->run(400);
  r->press(kW);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadTriangle, 300));
  return 0;
}

static int test_gesture_release(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross).gesture = Gesture::Release;
  r->apply();
  r->press(kW);
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 700)); /* held, even long */
  r->release(kW);
  KASSERT_RANGE(60, 75, r->on_ms(kPadCross, 300));
  return 0;
}

/* ---- modes ----------------------------------------------------------- */

static int test_mode_turbo(void) {
  struct Case { uint8_t hz, duty; int edges_lo, edges_hi; double f_lo, f_hi; };
  const Case cases[] = {{10, 50, 19, 21, 0.45, 0.55},
                        {5, 20, 9, 11, 0.16, 0.24},
                        {30, 90, 58, 62, 0.80, 0.96}};
  for (const Case &c : cases) {
    auto r = NewRig();
    Binding &b = r->btn(kW, kPadCross);
    b.mode = Mode::Turbo;
    b.turbo_hz = c.hz;
    b.turbo_duty = c.duty;
    r->apply();
    r->press(kW);
    /* The first read after the press is already on. */
    TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);
    int on = 1;
    int edges = 1;
    bool prev = true;
    for (int t = 1; t < 2000; ++t) {
      bool now_on = (r->tick().buttons & kPadCross) != 0;
      on += now_on;
      edges += now_on && !prev;
      prev = now_on;
    }
    KASSERT_RANGE(c.edges_lo, c.edges_hi, edges);
    const double frac = on / 2000.0;
    if (frac < c.f_lo || frac > c.f_hi)
      return onion_test_fail(__FILE__, __LINE__, "turbo %d Hz %d%%: on %.3f",
                             c.hz, c.duty, frac);
    r->release(kW);
    r->tick();
    TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 300));
  }
  return 0;
}

static int test_mode_toggle(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross).mode = Mode::Toggle;
  r->apply();
  r->press(kW);
  r->run(40);
  r->release(kW);
  TEST_ASSERT_EQ_INT(500, r->on_ms(kPadCross, 500));
  r->press(kW);
  r->run(40);
  r->release(kW);
  r->tick();
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCross, 500));
  /* A long hold is one flip, not one per read. */
  r->press(kW);
  r->run(800);
  r->release(kW);
  TEST_ASSERT_EQ_INT(300, r->on_ms(kPadCross, 300));
  return 0;
}

static int test_mode_pulse(void) {
  auto r = NewRig();
  Binding &b = r->btn(kW, kPadCross);
  b.mode = Mode::Pulse;
  b.pulse_ms = 60;
  r->apply();
  r->press(kW);
  bool was = false;
  int edges = 0, on = 0;
  for (int t = 0; t < 1000; ++t) {
    bool now_on = (r->tick().buttons & kPadCross) != 0;
    edges += now_on && !was;
    on += now_on;
    was = now_on;
  }
  TEST_ASSERT_EQ_INT(1, edges);
  KASSERT_RANGE(60, 62, on);
  r->release(kW);
  r->run(50);
  /* A press shorter than the pulse still gives the whole pulse. */
  r->press(kW);
  int on2 = (r->tick().buttons & kPadCross) ? 1 : 0;
  r->release(kW);
  on2 += r->on_ms(kPadCross, 200);
  KASSERT_RANGE(60, 62, on2);
  return 0;
}

/* ---- sticks: SOCD, diagonal, scale --------------------------------------- */

static int socd_rig(std::unique_ptr<Rig> &r, Opposing how) {
  r = NewRig();
  r->blob.layers[0].opposing = how;
  r->add(kA, OAxis(kAxisLX, -32767));
  r->add(kD, OAxis(kAxisLX, 32767));
  r->add(kW, OAxis(kAxisLY, -32767));
  r->add(kS, OAxis(kAxisLY, 32767));
  r->apply();
  return 0;
}

static int test_socd_last_wins(void) {
  std::unique_ptr<Rig> r;
  socd_rig(r, Opposing::LastWins);
  r->press(kA);
  KASSERT_RANGE(0, 1, r->tick().lx);
  TEST_ASSERT_EQ_INT(1, r->out.active);
  r->press(kD);
  TEST_ASSERT_EQ_INT(255, r->tick().lx);
  r->release(kD);
  KASSERT_RANGE(0, 1, r->tick().lx); /* A still held takes over again */
  r->release(kA);
  TEST_ASSERT_EQ_INT(128, r->tick().lx);
  /* Other order, other axis. */
  r->press(kS);
  r->tick();
  r->press(kW);
  KASSERT_RANGE(0, 1, r->tick().ly);
  TEST_ASSERT_EQ_INT(128, r->out.lx);
  return 0;
}

static int test_socd_neutral(void) {
  std::unique_ptr<Rig> r;
  socd_rig(r, Opposing::Neutral);
  r->press(kA);
  r->tick();
  r->press(kD);
  TEST_ASSERT_EQ_INT(128, r->tick().lx);
  r->release(kA);
  TEST_ASSERT_EQ_INT(255, r->tick().lx);
  return 0;
}

static int test_socd_first_wins(void) {
  std::unique_ptr<Rig> r;
  socd_rig(r, Opposing::FirstWins);
  r->press(kA);
  r->tick();
  r->press(kD);
  KASSERT_RANGE(0, 1, r->tick().lx);
  r->release(kA);
  TEST_ASSERT_EQ_INT(255, r->tick().lx);
  r->press(kA);
  TEST_ASSERT_EQ_INT(255, r->tick().lx);
  return 0;
}

static int test_diagonal_on_circle(void) {
  std::unique_ptr<Rig> r;
  socd_rig(r, Opposing::LastWins);
  r->press(kD);
  TEST_ASSERT_EQ_INT(255, r->tick().lx); /* one axis alone is full */
  r->press(kW);
  r->tick();
  const double x = Defl(r->out.lx), y = Defl(r->out.ly);
  const double mag = std::sqrt(x * x + y * y);
  if (mag > 1.02 || mag < 0.96)
    return onion_test_fail(__FILE__, __LINE__, "diagonal (%d,%d) |d|=%.3f",
                           r->out.lx, r->out.ly, mag);
  KASSERT_RANGE(212, 224, r->out.lx);
  KASSERT_RANGE(32, 44, r->out.ly);
  return 0;
}

static int test_stick_scale_walk(void) {
  std::unique_ptr<Rig> r;
  socd_rig(r, Opposing::LastWins);
  r->add(kLAlt, OScale(0, 16384));
  r->add(kLCtrl, OScale(1, 16384)); /* the other stick: no effect on left */
  r->apply();
  r->press(kW);
  r->press(kLAlt);
  KASSERT_RANGE(61, 68, r->tick().ly);
  r->press(kD);
  r->tick();
  const double x = Defl(r->out.lx), y = Defl(r->out.ly);
  const double mag = std::sqrt(x * x + y * y);
  if (mag < 0.46 || mag > 0.54)
    return onion_test_fail(__FILE__, __LINE__, "walk diagonal |d|=%.3f", mag);
  r->release(kD);
  r->release(kLAlt);
  KASSERT_RANGE(0, 1, r->tick().ly);
  r->press(kLCtrl);
  KASSERT_RANGE(0, 1, r->tick().ly);
  return 0;
}

/* ---- racing ramps ------------------------------------------------------ */

static int test_trigger_values_and_bit(void) {
  auto r = NewRig();
  r->add(kW, OTrigger(kTriggerR2, 32767));
  r->add(kS, OTrigger(kTriggerL2, 16384));
  r->add(kE, OTrigger(kTriggerL2, 2000)); /* ~16: under the threshold */
  r->apply();
  r->press(kW);
  r->tick();
  TEST_ASSERT_EQ_INT(255, r->out.r2);
  TEST_ASSERT_EQ_U64(kPadR2, r->out.buttons);
  r->release(kW);
  r->tick();
  TEST_ASSERT_EQ_INT(0, r->out.r2);
  TEST_ASSERT_EQ_U64(0, r->out.buttons);
  r->press(kS);
  r->tick();
  KASSERT_RANGE(126, 130, r->out.l2);
  TEST_ASSERT_EQ_U64(kPadL2, r->out.buttons);
  r->release(kS);
  r->press(kE);
  r->tick();
  KASSERT_RANGE(14, 18, r->out.l2);
  TEST_ASSERT_EQ_U64(0, r->out.buttons);
  TEST_ASSERT_EQ_INT(1, r->out.active);

  /* The threshold is the profile's. */
  auto t = NewRig();
  t->blob.trigger_button_threshold = 200;
  t->add(kS, OTrigger(kTriggerL2, 16384));
  t->apply();
  t->press(kS);
  t->tick();
  KASSERT_RANGE(126, 130, t->out.l2);
  TEST_ASSERT_EQ_U64(0, t->out.buttons);
  return 0;
}

/* Rise over 200 ms with `curve`, fall over 100 ms. */
static int trigger_ramp_case(uint16_t curve, int early_lo, int early_hi, int mid_lo,
                             int mid_hi) {
  auto r = NewRig();
  r->add(kW, OTrigger(kTriggerR2, 32767, Ramp{200, 100, curve, 0}));
  r->apply();
  r->press(kW);
  int prev = 0;
  for (int t = 1; t <= 210; ++t) {
    const PadState &s = r->tick();
    if (s.r2 < prev)
      return onion_test_fail(__FILE__, __LINE__, "curve %d: rise fell at %d ms", curve, t);
    prev = s.r2;
    if (s.r2 > 33 && !(s.buttons & kPadR2))
      return onion_test_fail(__FILE__, __LINE__, "r2 %d without bit", s.r2);
    if (s.r2 < 27 && (s.buttons & kPadR2))
      return onion_test_fail(__FILE__, __LINE__, "bit with r2 %d", s.r2);
    if (t == 5) KASSERT_RANGE(early_lo, early_hi, s.r2); /* not jumping */
    if (t == 100) KASSERT_RANGE(mid_lo, mid_hi, s.r2);
    if (t >= 205) TEST_ASSERT_EQ_INT(255, s.r2);
  }
  r->release(kW);
  prev = 255;
  for (int t = 1; t <= 110; ++t) {
    const PadState &s = r->tick();
    if (s.r2 > prev)
      return onion_test_fail(__FILE__, __LINE__, "fall rose at %d ms", t);
    prev = s.r2;
    /* the curve shapes the level both ways; only a linear fall is at half */
    if (t == 50 && curve == 100) KASSERT_RANGE(110, 145, s.r2);
    if (t == 50) KASSERT_RANGE(1, 254, s.r2);
    if (t >= 105) TEST_ASSERT_EQ_INT(0, s.r2);
    if (s.r2 < 27 && (s.buttons & kPadR2))
      return onion_test_fail(__FILE__, __LINE__, "bit stays at r2 %d", s.r2);
  }
  return 0;
}

/* at 5 ms (2.5 %%) and 100 ms (50 %%): linear 6 / 128, x^3 0 / 32, x^0.5 40 / 180 */
static int test_trigger_ramp_linear(void) { return trigger_ramp_case(100, 1, 20, 115, 140); }
static int test_trigger_ramp_progressive(void) { return trigger_ramp_case(300, 0, 5, 10, 100); }
static int test_trigger_ramp_fast_start(void) { return trigger_ramp_case(50, 20, 70, 150, 220); }

static int test_steering_ramp(void) {
  auto r = NewRig();
  r->add(kD, OAxis(kAxisLX, 32767, Ramp{100, 100, 100, 0}));
  r->add(kA, OAxis(kAxisLX, -32767, Ramp{100, 100, 100, 0}));
  r->apply();
  r->press(kD);
  r->run(49);
  KASSERT_RANGE(180, 203, r->tick().lx);
  r->run(54);
  TEST_ASSERT_EQ_INT(255, r->tick().lx);
  r->release(kD);
  r->run(49);
  KASSERT_RANGE(180, 203, r->tick().lx);
  r->run(54);
  TEST_ASSERT_EQ_INT(128, r->tick().lx);
  r->press(kA);
  r->run(49);
  KASSERT_RANGE(53, 76, r->tick().lx);
  r->run(54);
  KASSERT_RANGE(0, 1, r->tick().lx);
  return 0;
}

/* ---- macros ---------------------------------------------------------- */

static void add_macro(Rig &r, uint8_t repeat) {
  MacroStep *s = r.blob.macro_steps;
  for (int i = 0; i < 3; ++i) {
    s[i] = MacroStep{};
    for (int a = 0; a < 4; ++a) s[i].axes[a] = INT16_MIN;
    s[i].duration_ms = 50;
  }
  s[0].buttons = kPadCross;
  s[2].buttons = kPadCircle;
  r.blob.macro_step_count = 3;
  r.blob.macros[0] = Macro{0, 3, repeat, {}};
  r.blob.macro_count = 1;
  r.add(kM, OMacro(0));
}

static int test_macro_once(void) {
  auto r = NewRig();
  add_macro(*r, 0);
  r->apply();
  r->press(kM);
  int cross = 0, circle = 0, both = 0;
  bool was = false;
  int cross_edges = 0;
  for (int t = 0; t < 1000; ++t) {
    uint32_t b = r->tick().buttons;
    cross += (b & kPadCross) != 0;
    circle += (b & kPadCircle) != 0;
    both += (b & kPadCross) && (b & kPadCircle);
    cross_edges += (b & kPadCross) && !was;
    was = b & kPadCross;
    if (t > 170 && b) return onion_test_fail(__FILE__, __LINE__, "still running at %d", t);
  }
  TEST_ASSERT_EQ_INT(1, cross_edges);
  KASSERT_RANGE(48, 52, cross);
  KASSERT_RANGE(48, 52, circle);
  TEST_ASSERT_EQ_INT(0, both);
  r->release(kM);
  r->run(10);
  /* A brief press runs the macro to the end (G5 "once"). */
  r->press(kM);
  r->tick();
  r->release(kM);
  TEST_ASSERT_TRUE(r->on_ms(kPadCircle, 300) >= 48);
  return 0;
}

static int test_macro_repeat_while_held(void) {
  auto r = NewRig();
  add_macro(*r, 1);
  r->apply();
  r->press(kM);
  bool was = false;
  int edges = r->rising_edges(kPadCross, 1000, &was);
  KASSERT_RANGE(6, 7, edges); /* period 150 ms */
  r->release(kM);
  r->run(200);
  for (int i = 0; i < 500; ++i) TEST_ASSERT_TRUE(Idle(r->tick()));
  return 0;
}

static int test_macro_axes_untouched(void) {
  auto r = NewRig();
  MacroStep &s = r->blob.macro_steps[0];
  s = MacroStep{};
  s.axes[0] = 32767;
  s.axes[1] = s.axes[2] = s.axes[3] = INT16_MIN;
  s.l2 = 200;
  s.duration_ms = 100;
  r->blob.macro_step_count = 1;
  r->blob.macros[0] = Macro{0, 1, 0, {}};
  r->blob.macro_count = 1;
  r->add(kM, OMacro(0));
  r->add(kW, OAxis(kAxisLY, -32767));
  r->apply();
  r->press(kM);
  r->run(20);
  const PadState &o = r->tick();
  TEST_ASSERT_EQ_INT(255, o.lx);
  TEST_ASSERT_EQ_INT(128, o.ly);
  TEST_ASSERT_EQ_INT(128, o.rx);
  TEST_ASSERT_EQ_INT(128, o.ry);
  TEST_ASSERT_EQ_INT(200, o.l2);
  /* W keeps working under the macro: the stick goes up-right (circle). */
  r->press(kW);
  r->tick();
  KASSERT_RANGE(212, 224, r->out.lx);
  KASSERT_RANGE(32, 44, r->out.ly);
  return 0;
}

/* ---- wheel ----------------------------------------------------------- */

static int test_wheel_pulses(void) {
  auto r = NewRig();
  r->btn(kWheelUp, kPadCross);
  r->btn(kWheelDown, kPadCircle);
  r->btn(kWheelRight, kPadR1);
  r->btn(kWheelLeft, kPadL1);
  r->apply();
  r->f.wheel_v = 1; /* positive: up */
  KASSERT_RANGE(60, 75, r->on_ms(kPadCross, 300));
  TEST_ASSERT_EQ_INT(0, r->on_ms(kPadCircle, 1));
  r->f.wheel_v = -1;
  KASSERT_RANGE(60, 75, r->on_ms(kPadCircle, 300));
  r->f.wheel_h = 1; /* positive: right */
  KASSERT_RANGE(60, 75, r->on_ms(kPadR1, 300));
  /* Two notches in one frame are two presses, not one merged one. */
  r->f.wheel_v = 2;
  bool was = false;
  TEST_ASSERT_EQ_INT(2, r->rising_edges(kPadCross, 600, &was));
  TEST_ASSERT_TRUE(Idle(r->tick()));
  return 0;
}

/* ---- mouse ----------------------------------------------------------- */

/* Steady motion of (dx, dy) per frame of step_ms; mean rx/ry of the last 8. */
static void steady(Rig &r, int dx, int dy, uint32_t step_ms, double *x, double *y) {
  double sx = 0, sy = 0;
  for (int i = 0; i < 60; ++i) {
    r.f.mouse_dx = dx;
    r.f.mouse_dy = dy;
    const PadState &s = r.tick(step_ms);
    if (i >= 52) {
      sx += s.rx;
      sy += s.ry;
    }
  }
  *x = sx / 8.0 - 128.0;
  *y = sy / 8.0 - 128.0;
}

static int test_mouse_zero_motion_exact_centre(void) {
  auto r = NewRig();
  r->blob.layers[0].mouse = VelocityMouse();
  r->blob.layers[0].mouse.deadzone_x1000 = 200;
  r->apply();
  for (int i = 0; i < 100; ++i) TEST_ASSERT_TRUE(Idle(r->tick(4)));
  /* After motion: back to exact centre, and no dither residue later. */
  for (int i = 0; i < 20; ++i) {
    r->f.mouse_dx = 7;
    r->f.mouse_dy = -3;
    r->tick(4);
  }
  r->run(200, 4);
  for (int i = 0; i < 250; ++i) TEST_ASSERT_TRUE(Idle(r->tick(4)));
  return 0;
}

static int test_mouse_deadzone_compensation(void) {
  for (int sign = -1; sign <= 1; sign += 2) {
    auto r = NewRig();
    r->blob.layers[0].mouse = VelocityMouse();
    r->blob.layers[0].mouse.deadzone_x1000 = 200;
    r->apply();
    r->tick(4);
    r->f.mouse_dx = sign;
    bool moved = false;
    for (int i = 0; i < 30; ++i) {
      const PadState &s = r->tick(4);
      TEST_ASSERT_EQ_INT(128, s.ry);
      if (s.rx == 128) continue;
      moved = true;
      /* never inside the game's deadzone: 0.2 * 127 = 25.4 */
      if (sign > 0) KASSERT_RANGE(153, 254, s.rx);
      else KASSERT_RANGE(1, 103, s.rx);
    }
    TEST_ASSERT_TRUE(moved);
  }
  /* Square deadzone: the compensation applies per axis. */
  auto q = NewRig();
  q->blob.layers[0].mouse = VelocityMouse();
  q->blob.layers[0].mouse.deadzone_x1000 = 200;
  q->blob.layers[0].mouse.deadzone_shape = DeadzoneShape::Square;
  q->apply();
  q->f.mouse_dx = 1;
  q->f.mouse_dy = 1;
  const PadState &s = q->tick(4);
  KASSERT_RANGE(153, 254, s.rx);
  KASSERT_RANGE(153, 254, s.ry);
  return 0;
}

static int test_mouse_saturation_and_mirror(void) {
  for (int32_t big : {100000, INT32_MAX}) {
    auto r = NewRig();
    r->blob.layers[0].mouse = VelocityMouse();
    r->apply();
    r->f.mouse_dx = big;
    TEST_ASSERT_EQ_INT(255, r->tick(4).rx);
    TEST_ASSERT_EQ_INT(128, r->out.ry);
    r->f.mouse_dx = -big;
    r->tick(4);
    r->f.mouse_dx = -big;
    KASSERT_RANGE(0, 1, r->tick(4).rx);
    r->f.mouse_dy = big;
    r->f.mouse_dx = big;
    r->tick(4);
    const double x = Defl(r->out.rx), y = Defl(r->out.ry);
    if (std::sqrt(x * x + y * y) > 1.02)
      return onion_test_fail(__FILE__, __LINE__, "diagonal saturation off circle (%d,%d)",
                             r->out.rx, r->out.ry);
  }
  /* Mirrored motion, mirrored output. */
  for (int k : {1, 3, 10, 40}) {
    auto p = NewRig(), n = NewRig();
    p->blob.layers[0].mouse = VelocityMouse();
    n->blob.layers[0].mouse = VelocityMouse();
    p->apply();
    n->apply();
    double px, py, nx, ny;
    steady(*p, k, 0, 4, &px, &py);
    steady(*n, -k, 0, 4, &nx, &ny);
    if (std::fabs(px + nx) > 1.01)
      return onion_test_fail(__FILE__, __LINE__, "k=%d: +%.2f vs %.2f", k, px, nx);
  }
  return 0;
}

/* dx per 4 ms frame that gives a mid deflection (20..50 of 127), or 0. */
static int find_mid_motion(void) {
  for (int k = 1; k <= 400; ++k) {
    auto r = NewRig();
    r->blob.layers[0].mouse = VelocityMouse();
    r->apply();
    double x, y;
    steady(*r, k, 0, 4, &x, &y);
    if (x >= 20 && x <= 50) return k;
    if (x > 50) return 0;
  }
  return 0;
}

static int test_mouse_yx_ratio_and_invert(void) {
  const int k = find_mid_motion();
  if (!k) return onion_test_fail(__FILE__, __LINE__, "no dx gives a mid deflection");
  auto a = NewRig();
  a->blob.layers[0].mouse = VelocityMouse();
  a->apply();
  double x, y, x2, y2;
  steady(*a, k, 0, 4, &x, &y);
  TEST_ASSERT_TRUE(std::fabs(y) < 0.5);

  auto b = NewRig();
  b->blob.layers[0].mouse = VelocityMouse();
  b->blob.layers[0].mouse.yx_ratio_x100 = 200;
  b->apply();
  steady(*b, 0, k, 4, &x2, &y2);
  if (y2 / x < 1.6 || y2 / x > 2.4)
    return onion_test_fail(__FILE__, __LINE__, "Y:X 2.0 gave x %.1f y %.1f", x, y2);
  /* Y down positive, like the pad. */
  TEST_ASSERT_TRUE(y2 > 0);

  auto ix = NewRig();
  ix->blob.layers[0].mouse = VelocityMouse();
  ix->blob.layers[0].mouse.invert = 1;
  ix->apply();
  steady(*ix, k, k, 4, &x2, &y2);
  TEST_ASSERT_TRUE(x2 < -10 && y2 > 10);

  auto iy = NewRig();
  iy->blob.layers[0].mouse = VelocityMouse();
  iy->blob.layers[0].mouse.invert = 2;
  iy->apply();
  steady(*iy, k, k, 4, &x2, &y2);
  TEST_ASSERT_TRUE(x2 > 10 && y2 < -10);

  /* Sensitivity scales it. */
  auto s = NewRig();
  s->blob.layers[0].mouse = VelocityMouse();
  s->blob.layers[0].mouse.sensitivity_x100 = 50;
  s->apply();
  steady(*s, k, 0, 4, &x2, &y2);
  if (x2 / x < 0.4 || x2 / x > 0.6)
    return onion_test_fail(__FILE__, __LINE__, "sens 0.5 gave %.1f vs %.1f", x2, x);
  return 0;
}

static int test_mouse_read_rate_independent(void) {
  const int k = find_mid_motion();
  if (!k) return onion_test_fail(__FILE__, __LINE__, "no dx gives a mid deflection");
  auto fast = NewRig(), slow = NewRig();
  fast->blob.layers[0].mouse = VelocityMouse();
  slow->blob.layers[0].mouse = VelocityMouse();
  fast->apply();
  slow->apply();
  double fx, fy, sx, sy;
  steady(*fast, k, 0, 4, &fx, &fy);
  steady(*slow, 4 * k, 0, 16, &sx, &sy); /* same speed, 60 Hz reads */
  if (std::fabs(fx - sx) > 3)
    return onion_test_fail(__FILE__, __LINE__, "250 Hz %.1f vs 60 Hz %.1f", fx, sx);
  return 0;
}

static int test_mouse_stops_within_window(void) {
  for (uint16_t window : {12, 40}) {
    auto r = NewRig();
    r->blob.layers[0].mouse = VelocityMouse();
    r->blob.layers[0].mouse.smoothing_ms = window;
    r->blob.layers[0].mouse.deadzone_x1000 = 150;
    r->apply();
    for (int i = 0; i < 100; ++i) {
      r->f.mouse_dx = 20;
      r->f.mouse_dy = 10;
      r->tick(2);
    }
    int stopped_after = -1;
    for (int t = 2; t <= 300; t += 2) {
      const PadState &s = r->tick(2);
      const bool centre = s.rx == 128 && s.ry == 128;
      if (centre && stopped_after < 0) stopped_after = t;
      if (!centre && stopped_after >= 0)
        return onion_test_fail(__FILE__, __LINE__, "moved again at %d ms", t);
    }
    /* window, plus a short hard stop and one read */
    if (stopped_after < 0 || stopped_after > window + 10)
      return onion_test_fail(__FILE__, __LINE__, "window %d: stopped after %d ms",
                             window, stopped_after);
  }
  return 0;
}

static int test_mouse_layer_inherit_and_off(void) {
  auto r = NewRig();
  r->blob.layers[0].mouse = VelocityMouse();
  r->add(kQ, OLayer(OutputKind::LayerHold, 1));
  r->add(kE, OLayer(OutputKind::LayerHold, 2));
  r->blob.layer_count = 3;
  r->blob.layers[1].mouse_inherit = 1;
  r->blob.layers[2].mouse_inherit = 0;
  r->blob.layers[2].mouse = MouseParams{}; /* Off */
  r->apply();
  r->press(kQ);
  r->tick();
  double x, y;
  steady(*r, 30, 0, 4, &x, &y);
  TEST_ASSERT_TRUE(x > 10);
  r->release(kQ);
  r->press(kE);
  r->run(100, 4);
  steady(*r, 30, 0, 4, &x, &y);
  TEST_ASSERT_TRUE(x == 0 && y == 0);
  return 0;
}

static int test_mouse_position_spring(void) {
  auto r = NewRig();
  MouseParams m = VelocityMouse();
  m.mode = MouseMode::Position;
  m.stick = 0;
  m.position_range = 1000;
  m.spring_per_s_x10 = 0;
  r->blob.layers[0].mouse = m;
  r->apply();
  TEST_ASSERT_TRUE(Idle(r->tick(4)));
  r->f.mouse_dx = 500;
  KASSERT_RANGE(180, 203, r->tick(4).lx);
  TEST_ASSERT_EQ_INT(128, r->out.ly);
  /* No spring: the position holds. */
  r->run(500, 4);
  KASSERT_RANGE(180, 203, r->tick(4).lx);
  r->f.mouse_dx = -1000; /* back past centre to -0.5 */
  KASSERT_RANGE(53, 76, r->tick(4).lx);
  r->f.mouse_dx = -100000; /* clamps at the rim, no wrap */
  KASSERT_RANGE(0, 1, r->tick(4).lx);
  r->f.mouse_dx = 1000; /* from the rim, not from -100 */
  KASSERT_RANGE(120, 136, r->tick(4).lx);

  auto s = NewRig();
  m.spring_per_s_x10 = 50; /* 5 per s */
  s->blob.layers[0].mouse = m;
  s->apply();
  s->f.mouse_dx = 1000;
  KASSERT_RANGE(248, 255, s->tick(4).lx); /* the spring acts within this read too */
  int prev = s->out.lx;
  for (int t = 4; t <= 1500; t += 4) {
    const PadState &o = s->tick(4);
    if (o.lx > prev)
      return onion_test_fail(__FILE__, __LINE__, "spring went out at %d ms", t);
    prev = o.lx;
  }
  KASSERT_RANGE(128, 130, prev);
  return 0;
}

static int test_mouse_motion_as_digital(void) {
  auto r = NewRig();
  r->blob.layers[0].mouse_digital_threshold = 1000; /* counts/s */
  r->btn(kMouseMoveRight, kPadR1);
  r->btn(kMouseMoveLeft, kPadL1);
  r->apply();
  int on = 0;
  for (int i = 0; i < 50; ++i) {
    r->f.mouse_dx = 40; /* 10000 counts/s */
    on += (r->tick(4).buttons & kPadR1) != 0;
  }
  TEST_ASSERT_TRUE(on >= 45);
  TEST_ASSERT_EQ_U64(0, r->out.buttons & kPadL1);
  r->run(150, 4);
  TEST_ASSERT_EQ_U64(0, r->tick(4).buttons);
  on = 0;
  for (int i = 0; i < 50; ++i) {
    r->f.mouse_dx = 1; /* 250 counts/s: under */
    on += (r->tick(4).buttons & kPadR1) != 0;
  }
  TEST_ASSERT_EQ_INT(0, on);
  return 0;
}

/* ---- suspend and reset ------------------------------------------------ */

static int test_suspend_key(void) {
  auto r = NewRig();
  r->btn(kW, kPadCross);
  r->btn(kPause, kPadSquare); /* must be ignored: never remappable */
  r->add(kF12, OKind(OutputKind::MappingToggle));
  r->blob.layers[0].mouse = VelocityMouse();
  r->apply();

  r->press(kPause);
  TEST_ASSERT_EQ_U64(0, r->tick().buttons & kPadSquare);
  r->release(kPause);
  r->tick();
  r->press(kW);
  r->f.mouse_dx = 50;
  for (int i = 0; i < 50; ++i) TEST_ASSERT_TRUE(Idle(r->tick()));
  r->release(kW);
  r->tick();
  r->press(kPause);
  r->tick();
  r->release(kPause);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);

  /* Suspending while a key is held lets go of it at once. */
  r->press(kPause);
  TEST_ASSERT_TRUE(Idle(r->tick()));
  r->release(kPause);
  TEST_ASSERT_TRUE(Idle(r->tick()));
  r->release(kW);
  r->press(kPause);
  r->tick();
  r->release(kPause);
  r->tick();

  /* A MappingToggle binding suspends, and still resumes while suspended. */
  r->press(kF12);
  r->tick();
  r->release(kF12);
  r->tick();
  r->press(kW);
  TEST_ASSERT_TRUE(Idle(r->tick()));
  r->release(kW);
  r->tick();
  r->press(kF12);
  r->tick();
  r->release(kF12);
  r->tick();
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCross, r->tick().buttons);

  /* suspend_key 0: no suspend key, Pause is an ordinary key. */
  auto z = NewRig();
  z->blob.suspend_key = kInputNone;
  z->btn(kPause, kPadSquare);
  z->apply();
  z->press(kPause);
  TEST_ASSERT_EQ_U64(kPadSquare, z->tick().buttons);
  return 0;
}

static int test_reset_releases_everything(void) {
  auto r = NewRig();
  r->add(kT, OLayer(OutputKind::LayerToggle, 1));
  r->btn(kW, kPadCircle);
  r->btn(kW, kPadCross, 1);
  r->btn(kG, kPadSquare).mode = Mode::Toggle;
  r->add(kD, OAxis(kAxisLX, 32767, Ramp{0, 500, 100, 0}));
  r->add(kS, OTrigger(kTriggerR2, 32767, Ramp{0, 500, 100, 0}));
  add_macro(*r, 1);
  r->blob.layers[0].mouse = VelocityMouse();
  r->apply();

  r->press(kT);
  r->tick();
  r->release(kT);
  r->press(kG);
  r->tick();
  r->release(kG);
  r->press(kW);
  r->press(kD);
  r->press(kS);
  r->press(kM);
  r->f.mouse_dx = 40;
  r->f.wheel_v = 2;
  r->run(30);
  TEST_ASSERT_TRUE(r->out.buttons & kPadCross);
  TEST_ASSERT_TRUE(r->out.buttons & kPadSquare);

  r->eng.Reset();
  std::memset(r->f.down, 0, sizeof(r->f.down));
  /* Nothing left: no fall ramp, no macro, no toggle, no layer, no motion. */
  for (int i = 0; i < 20; ++i) TEST_ASSERT_TRUE(Idle(r->tick()));
  r->press(kW);
  TEST_ASSERT_EQ_U64(kPadCircle, r->tick().buttons);
  return 0;
}

extern "C" int test_kbm_engine_suite(void) {
  int failures = 0;
  failures += onion_test_run("kbm_idle_inactive", test_idle_inactive);
  failures += onion_test_run("kbm_disabled_or_bad_blob_inactive",
                             test_disabled_or_bad_blob_inactive);
  failures += onion_test_run("kbm_corrupt_blob_no_crash", test_corrupt_blob_no_crash);
  failures += onion_test_run("kbm_key_gives_button", test_key_gives_button);
  failures += onion_test_run("kbm_chord_priority", test_chord_priority);
  failures += onion_test_run("kbm_chord_order_independent",
                             test_chord_order_independent_of_binding_order);
  failures += onion_test_run("kbm_layer_hold_fallthrough", test_layer_hold_and_fallthrough);
  failures += onion_test_run("kbm_layer_none_blocks", test_layer_none_blocks);
  failures += onion_test_run("kbm_layer_latched", test_layer_latched_until_release);
  failures += onion_test_run("kbm_layer_toggle", test_layer_toggle);
  failures += onion_test_run("kbm_layer_toggle_key_latched", test_layer_toggle_key_latched);
  failures += onion_test_run("kbm_gesture_tap", test_gesture_tap);
  failures += onion_test_run("kbm_gesture_long_press", test_gesture_long_press);
  failures += onion_test_run("kbm_gesture_tap_long_exclusive",
                             test_gesture_tap_and_long_exclusive);
  failures += onion_test_run("kbm_gesture_double_tap", test_gesture_double_tap);
  failures += onion_test_run("kbm_gesture_release", test_gesture_release);
  failures += onion_test_run("kbm_mode_turbo", test_mode_turbo);
  failures += onion_test_run("kbm_mode_toggle", test_mode_toggle);
  failures += onion_test_run("kbm_mode_pulse", test_mode_pulse);
  failures += onion_test_run("kbm_socd_last_wins", test_socd_last_wins);
  failures += onion_test_run("kbm_socd_neutral", test_socd_neutral);
  failures += onion_test_run("kbm_socd_first_wins", test_socd_first_wins);
  failures += onion_test_run("kbm_diagonal_on_circle", test_diagonal_on_circle);
  failures += onion_test_run("kbm_stick_scale_walk", test_stick_scale_walk);
  failures += onion_test_run("kbm_trigger_values_bit", test_trigger_values_and_bit);
  failures += onion_test_run("kbm_trigger_ramp_linear", test_trigger_ramp_linear);
  failures += onion_test_run("kbm_trigger_ramp_progressive", test_trigger_ramp_progressive);
  failures += onion_test_run("kbm_trigger_ramp_fast_start", test_trigger_ramp_fast_start);
  failures += onion_test_run("kbm_steering_ramp", test_steering_ramp);
  failures += onion_test_run("kbm_macro_once", test_macro_once);
  failures += onion_test_run("kbm_macro_repeat", test_macro_repeat_while_held);
  failures += onion_test_run("kbm_macro_axes_untouched", test_macro_axes_untouched);
  failures += onion_test_run("kbm_wheel_pulses", test_wheel_pulses);
  failures += onion_test_run("kbm_mouse_zero_exact_centre", test_mouse_zero_motion_exact_centre);
  failures += onion_test_run("kbm_mouse_deadzone_comp", test_mouse_deadzone_compensation);
  failures += onion_test_run("kbm_mouse_saturation_mirror", test_mouse_saturation_and_mirror);
  failures += onion_test_run("kbm_mouse_yx_invert_sens", test_mouse_yx_ratio_and_invert);
  failures += onion_test_run("kbm_mouse_rate_independent", test_mouse_read_rate_independent);
  failures += onion_test_run("kbm_mouse_stops_in_window", test_mouse_stops_within_window);
  failures += onion_test_run("kbm_mouse_layer_inherit_off", test_mouse_layer_inherit_and_off);
  failures += onion_test_run("kbm_mouse_position_spring", test_mouse_position_spring);
  failures += onion_test_run("kbm_mouse_motion_digital", test_mouse_motion_as_digital);
  failures += onion_test_run("kbm_suspend_key", test_suspend_key);
  failures += onion_test_run("kbm_reset_releases", test_reset_releases_everything);
  return failures;
}
