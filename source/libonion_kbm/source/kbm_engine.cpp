/* Keyboard & mouse for games: the runtime engine (see kbm_engine.hpp).
 * Runs inside the game's pad read hook: no heap, no exceptions, no locks,
 * no logging, O(bindings) per Tick. */
#include <onion/kbm_engine.hpp>

namespace onion::kbm {
namespace {

using analog::Clamp;

constexpr int kBytes = (kInputCount + 7) / 8;
constexpr uint16_t kDefaultTapMs = 200, kDefaultLongMs = 400,
                   kDefaultDoubleMs = 250, kDefaultPulseMs = 60;
constexpr uint16_t kDefaultSmoothingMs = 12;
constexpr uint16_t kDefaultMoveThreshold = 1500; /* counts/s */
constexpr uint64_t kNever = ~0ull;

uint64_t Ms(uint16_t v, uint16_t def) { return uint64_t(v ? v : def) * 1000u; }

bool Bit(const uint8_t *bits, int i) { return bits[i >> 3] & (1u << (i & 7)); }

void SetBit(uint8_t *bits, int i, bool on) {
  if (on)
    bits[i >> 3] |= uint8_t(1u << (i & 7));
  else
    bits[i >> 3] &= uint8_t(~(1u << (i & 7)));
}

float Unit(int16_t v) { return Clamp(float(v) / 32767.f, -1.f, 1.f); }

uint8_t Byte(float v01) { return uint8_t(Clamp(v01, 0.f, 1.f) * 255.f + 0.5f); }

/* A pulse of `len` starts only after the previous one and an off-gap as long,
   so two events stay two presses the game can see. True when one starts. */
bool QueuePulse(uint64_t *end, uint8_t *pending, bool trigger, uint64_t now, uint64_t len) {
  if (trigger && *pending < 3)
    ++*pending;
  if (*pending && (!*end || now >= *end + len)) {
    --*pending;
    *end = now + len;
    return true;
  }
  return false;
}

} // namespace

void Engine::SetProfile(const ProfileBlob *p) {
  p_ = nullptr;
  binding_count_ = 0;
  layer_count_ = 1;
  __builtin_memset(first_, 0, sizeof first_);
  __builtin_memset(count_, 0, sizeof count_);
  __builtin_memset(valid_, 0, sizeof valid_);
  __builtin_memset(input_layer_, 0, sizeof input_layer_);
  __builtin_memset(input_mtoggle_, 0, sizeof input_mtoggle_);
  for (uint16_t &ms : wheel_pulse_ms_)
    ms = kDefaultPulseMs;

  if (p && p->magic == kBlobMagic && p->version == kBlobVersion &&
      p->size == sizeof(ProfileBlob)) {
    p_ = p;
    binding_count_ = p->binding_count < kMaxBindings ? p->binding_count : kMaxBindings;
    layer_count_ = p->layer_count < 1 ? 1 : p->layer_count > kMaxLayers ? kMaxLayers : p->layer_count;
    uint16_t wheel_max[kWheelInputs] = {};
    for (int b = 0; b < binding_count_; ++b) {
      const Binding &bd = p->bindings[b];
      bool ok = bd.input != kInputNone && bd.input < kInputCount &&
                bd.layer < layer_count_ && bd.output_count <= kMaxOutputsPerBinding;
      int mods = 0;
      for (Input m : bd.modifiers) {
        if (m == kInputNone)
          continue;
        ok = ok && m < kInputCount && m != bd.input;
        ++mods;
      }
      valid_[b] = ok;
      mod_count_[b] = uint8_t(mods);
      has_layer_out_[b] = 0;
      has_mtoggle_[b] = 0;
      if (!ok)
        continue;
      ++count_[bd.input];
      for (int k = 0; k < bd.output_count; ++k) {
        const OutputKind kind = bd.outputs[k].kind;
        if (kind == OutputKind::LayerHold || kind == OutputKind::LayerToggle) {
          has_layer_out_[b] = 1;
          SetBit(input_layer_, bd.input, true);
        } else if (kind == OutputKind::MappingToggle) {
          has_mtoggle_[b] = 1;
          SetBit(input_mtoggle_, bd.input, true);
        }
      }
      const int w = int(bd.input) - kWheelUp;
      if (w >= 0 && w < kWheelInputs && bd.pulse_ms > wheel_max[w])
        wheel_max[w] = bd.pulse_ms;
    }
    for (int w = 0; w < kWheelInputs; ++w)
      if (wheel_max[w])
        wheel_pulse_ms_[w] = wheel_max[w];
    /* group binding indices by input, in binding order */
    uint16_t next[kInputCount];
    uint16_t at = 0;
    for (int i = 0; i < kInputCount; ++i) {
      first_[i] = next[i] = at;
      at = uint16_t(at + count_[i]);
    }
    for (int b = 0; b < binding_count_; ++b)
      if (valid_[b])
        order_[next[p->bindings[b].input]++] = uint16_t(b);
  }
  ReleaseAll();
}

void Engine::Reset() { ReleaseAll(); }

void Engine::ReleaseAll() {
  __builtin_memset(in_, 0, sizeof in_);
  __builtin_memset(bs_, 0, sizeof bs_);
  __builtin_memset(level_, 0, sizeof level_);
  __builtin_memset(runs_, 0, sizeof runs_);
  __builtin_memset(wheel_queue_, 0, sizeof wheel_queue_);
  __builtin_memset(wheel_on_, 0, sizeof wheel_on_);
  __builtin_memset(wheel_end_us_, 0, sizeof wheel_end_us_);
  __builtin_memset(move_on_, 0, sizeof move_on_);
  __builtin_memset(dither_, 0, sizeof dither_);
  layer_toggled_ = layer_hold_ = 0;
  layer_mask_ = 1;
  mouse_layer_ = -1;
  mouse_.Reset();
  resync_ = true; /* keys still held must be pressed again */
}

uint8_t Engine::TopLayer(uint8_t mask) const {
  for (int l = kMaxLayers - 1; l > 0; --l)
    if (mask & (1u << l))
      return uint8_t(l);
  return 0;
}

int Engine::MouseLayer(uint8_t mask) const {
  for (int l = kMaxLayers - 1; l > 0; --l)
    if ((mask & (1u << l)) && !p_->layers[l].mouse_inherit)
      return l;
  return 0;
}

void Engine::OnPress(int input, uint8_t mask, uint64_t now) {
  InputState &is = in_[input];
  const uint64_t prev_release = is.release_us, prev_held = is.held_us;
  const bool prev_double = is.was_double;
  is.press_us = now;
  is.was_double = 0;
  uint32_t defer = 0;
  const int first = first_[input], n = count_[input];
  for (int j = 0; j < n; ++j) {
    BindState &s = bs_[order_[first + j]];
    s.armed = 0;
    s.dbl = 0;
  }
  if (input == p_->suspend_key) {
    is.defer_us = 0;
    return;
  }

  auto mods_held = [&](int b) {
    for (Input m : p_->bindings[b].modifiers)
      if (m != kInputNone && !Down(m))
        return false;
    return true;
  };
  for (int l = kMaxLayers - 1; l >= 0; --l) {
    if (!(mask & (1u << l)))
      continue;
    int best = -1;
    for (int j = 0; j < n; ++j) {
      const int b = order_[first + j];
      if (p_->bindings[b].layer == l && mod_count_[b] > best && mods_held(b))
        best = mod_count_[b];
    }
    if (best < 0)
      continue; /* nothing here: fall through to the layer below */
    for (int j = 0; j < n; ++j) {
      const int b = order_[first + j];
      const Binding &bd = p_->bindings[b];
      if (bd.layer != l || mod_count_[b] != best || !mods_held(b))
        continue;
      BindState &s = bs_[b];
      s.armed = 1;
      if (bd.gesture == Gesture::DoubleTap) {
        const uint64_t window = Ms(bd.double_ms, kDefaultDoubleMs);
        if (window > defer)
          defer = uint32_t(window);
        s.dbl = prev_release && !prev_double && now - prev_release <= window &&
                prev_held < Ms(bd.tap_ms, kDefaultTapMs);
        if (s.dbl)
          is.was_double = 1;
      }
    }
    break;
  }
  is.defer_us = defer;
}

void Engine::EvalBinding(int b, uint64_t now) {
  const Binding &bd = p_->bindings[b];
  BindState &s = bs_[b];
  const InputState &is = in_[bd.input];
  const bool down = Down(bd.input), released = Released(bd.input);
  const uint64_t pulse = Ms(bd.pulse_ms, kDefaultPulseMs);
  bool level = false, event = false;

  switch (bd.gesture) {
  case Gesture::Press:
    level = s.armed && down;
    break;
  case Gesture::LongPress:
    level = s.armed && down && now - is.press_us >= Ms(bd.long_ms, kDefaultLongMs);
    break;
  case Gesture::DoubleTap:
    level = s.armed && down && s.dbl;
    break;
  case Gesture::Tap: {
    bool fire = false;
    if (s.tap_deadline_us && Pressed(bd.input)) {
      /* pressed again before the double-tap window ran out: the DoubleTap
         takes it, otherwise the waiting tap fires now */
      fire = !is.was_double;
      s.tap_deadline_us = 0;
    }
    if (s.armed && released && !is.was_double &&
        is.held_us < Ms(bd.tap_ms, kDefaultTapMs)) {
      if (is.defer_us)
        s.tap_deadline_us = now + is.defer_us;
      else
        fire = true;
    }
    if (s.tap_deadline_us && now >= s.tap_deadline_us) {
      s.tap_deadline_us = 0;
      fire = true;
    }
    event = QueuePulse(&s.pulse_end_us, &s.pulse_pending, fire, now, pulse);
    level = now < s.pulse_end_us;
    break;
  }
  case Gesture::Release:
    event = QueuePulse(&s.pulse_end_us, &s.pulse_pending, s.armed && released, now, pulse);
    level = now < s.pulse_end_us;
    break;
  }

  const bool rise = event || (level && !s.firing);
  s.firing = level;
  bool out = level;
  switch (bd.mode) {
  case Mode::Hold:
    break;
  case Mode::Toggle:
    if (rise)
      s.latch ^= 1;
    out = s.latch;
    break;
  case Mode::Turbo: {
    /* Phases are counted per read: each on and each off phase lasts its time
       and at least one read, so no press is lost to a slow read rate. */
    const uint64_t hz = bd.turbo_hz ? (bd.turbo_hz > 30 ? 30 : bd.turbo_hz) : 10;
    const uint64_t duty = !bd.turbo_duty ? 50 : bd.turbo_duty < 10 ? 10 : bd.turbo_duty > 90 ? 90 : bd.turbo_duty;
    const uint64_t on_us = 1000000u / hz * duty / 100, off_us = 1000000u / hz - on_us;
    if (!level) {
      s.turbo_on = 0;
    } else if (rise) {
      s.turbo_on = 1;
      s.turbo_end_us = now + on_us;
    } else if (now >= s.turbo_end_us) {
      s.turbo_on ^= 1;
      s.turbo_end_us = now + (s.turbo_on ? on_us : off_us);
    }
    out = s.turbo_on;
    break;
  }
  case Mode::Pulse:
    QueuePulse(&s.mode_end_us, &s.mode_pending, rise, now, pulse);
    out = now < s.mode_end_us;
    break;
  }
  s.out_rise = out && !s.out;
  if (s.out_rise)
    s.on_since_us = now;
  s.out = out;
}

void Engine::StartMacro(int b, int output, uint64_t now) {
  const ProfileBlob &p = *p_;
  const uint8_t id = p.bindings[b].outputs[output].macro;
  const int macros = p.macro_count < kMaxMacros ? p.macro_count : kMaxMacros;
  const int steps = p.macro_step_count < kMaxMacroSteps ? p.macro_step_count : kMaxMacroSteps;
  if (id >= macros)
    return;
  const Macro &m = p.macros[id];
  if (!m.step_count || int(m.first_step) + int(m.step_count) > steps)
    return;
  int slot = -1;
  for (int r = 0; r < kMaxRunning; ++r) {
    if (runs_[r].active && runs_[r].owner == b && runs_[r].output == output)
      return; /* still running: a press during a run does not restart it */
    if (!runs_[r].active && slot < 0)
      slot = r;
  }
  if (slot < 0)
    return;
  runs_[slot] = {now, uint16_t(b), 0, uint8_t(output), id, 1};
}

void Engine::Tick(const InputFrame &in, PadState *out) {
  PadState o = {};
  o.lx = o.ly = o.rx = o.ry = 128;

  const uint64_t now = in.now_us > now_us_ ? in.now_us : now_us_;
  const uint64_t elapsed = have_time_ ? now - now_us_ : 0;
  const uint32_t dt_us = elapsed > 100000 ? 100000u : uint32_t(elapsed);
  const float dt_ms = float(dt_us) / 1000.f;
  /* after a gap (game paused, hook not called) the piled-up motion would be
     spread over the capped dt and become one big flick: drop it */
  const bool gap = elapsed > 100000;
  const int32_t mdx = gap ? 0 : in.mouse_dx, mdy = gap ? 0 : in.mouse_dy;
  now_us_ = now;
  have_time_ = true;

  if (!p_ || !p_->enabled) {
    if (!resync_)
      ReleaseAll();
    *out = o;
    return;
  }
  const ProfileBlob &p = *p_;

  /* ---- 1. this read's inputs ---------------------------------------- */
  __builtin_memcpy(down_, in.down, kBytes);
  SetBit(down_, kInputNone, false);
  for (int i = 0; i < kWheelInputs; ++i) {
    SetBit(down_, kWheelUp + i, false);
    SetBit(down_, kMouseMoveRight + i, false);
  }
  mouse_.Push(now, dt_us, mdx, mdy);

  /* each notch: pressed for pulse_ms, then released as long, so the game
     sees every one (a read happens at least every 2 frames) */
  const int notches[kWheelInputs] = {in.wheel_v > 0 ? in.wheel_v : 0,
                                     in.wheel_v < 0 ? -in.wheel_v : 0,
                                     in.wheel_h < 0 ? -in.wheel_h : 0,
                                     in.wheel_h > 0 ? in.wheel_h : 0};
  for (int w = 0; w < kWheelInputs; ++w) {
    const int q = wheel_queue_[w] + notches[w];
    wheel_queue_[w] = uint8_t(q > kWheelQueueCap ? kWheelQueueCap : q);
    if (now >= wheel_end_us_[w]) {
      if (wheel_on_[w]) {
        wheel_on_[w] = 0;
        wheel_end_us_[w] = now + wheel_pulse_ms_[w] * 1000ull;
      } else if (wheel_queue_[w]) {
        --wheel_queue_[w];
        wheel_on_[w] = 1;
        wheel_end_us_[w] = now + wheel_pulse_ms_[w] * 1000ull;
      }
    }
    SetBit(down_, kWheelUp + w, wheel_on_[w]);
  }

  {
    const Layer &ml = p.layers[MouseLayer(layer_mask_)];
    float vx, vy;
    mouse_.Velocity(now, ml.mouse.smoothing_ms ? ml.mouse.smoothing_ms : kDefaultSmoothingMs,
                    &vx, &vy);
    uint16_t threshold = kDefaultMoveThreshold;
    for (int l = TopLayer(layer_mask_); l >= 0; --l)
      if ((layer_mask_ & (1u << l)) && p.layers[l].mouse_digital_threshold) {
        threshold = p.layers[l].mouse_digital_threshold;
        break;
      }
    const float on_at = threshold / 1000.f, off_at = on_at * 0.7f; /* c/ms */
    const float speed[4] = {vx, -vx, vy, -vy}; /* right, left, down, up */
    for (int d = 0; d < 4; ++d) {
      move_on_[d] = speed[d] >= (move_on_[d] ? off_at : on_at);
      SetBit(down_, kMouseMoveRight + d, move_on_[d]);
    }
  }

  if (resync_) {
    __builtin_memcpy(prev_, down_, kBytes);
    resync_ = false;
  }
  for (int i = 0; i < kBytes; ++i) {
    pressed_[i] = uint8_t(down_[i] & ~prev_[i]);
    released_[i] = uint8_t(prev_[i] & ~down_[i]);
  }

  for (int i = 1; i < kInputCount; ++i)
    if (Released(i)) {
      in_[i].held_us = now - in_[i].press_us;
      in_[i].release_us = now;
    }

  /* ---- 2. suspend ----------------------------------------------------- */
  const int sk = p.suspend_key;
  if (sk != kInputNone && sk < kInputCount && Pressed(sk)) {
    if (!suspended_)
      suspend_mask_ = layer_mask_;
    suspended_ = !suspended_;
    ReleaseAll();
    __builtin_memcpy(prev_, down_, kBytes);
    resync_ = false; /* prev_ is current: the next press counts */
    *out = o;        /* active = 0 */
    return;
  }
  if (suspended_) {
    /* only the MappingToggle bindings run, with their own gesture,
       modifiers and layer */
    bool resume = false;
    for (int i = 1; i < kInputCount; ++i)
      if (Pressed(i) && Bit(input_mtoggle_, i))
        OnPress(i, suspend_mask_, now);
    for (int b = 0; b < binding_count_; ++b)
      if (valid_[b] && has_mtoggle_[b]) {
        EvalBinding(b, now);
        resume = resume || bs_[b].out_rise;
      }
    if (resume) {
      suspended_ = false;
      ReleaseAll();
    }
    __builtin_memcpy(prev_, down_, kBytes);
    resync_ = false;
    *out = o; /* active = 0 */
    return;
  }

  /* ---- 3./4. resolve presses, gestures, modes ------------------------- */

  const uint8_t layer_bits = uint8_t((1u << layer_count_) - 1u);
  uint8_t mask = uint8_t((1u | layer_toggled_ | layer_hold_) & layer_bits);
  for (int pass = 0; pass < 2; ++pass) {
    const bool layer_pass = pass == 0;
    for (int i = 1; i < kInputCount; ++i)
      if (Pressed(i) && Bit(input_layer_, i) == layer_pass)
        OnPress(i, mask, now);
    for (int b = 0; b < binding_count_; ++b)
      if (valid_[b] && bool(has_layer_out_[b]) == layer_pass)
        EvalBinding(b, now);
    if (!layer_pass)
      break;
    layer_hold_ = 0;
    for (int b = 0; b < binding_count_; ++b) {
      if (!valid_[b] || !has_layer_out_[b])
        continue;
      const Binding &bd = p.bindings[b];
      for (int k = 0; k < bd.output_count; ++k) {
        const Output &out_k = bd.outputs[k];
        if (out_k.layer == 0 || out_k.layer >= layer_count_)
          continue; /* the base layer is always on */
        const uint8_t bit = uint8_t(1u << out_k.layer);
        if (out_k.kind == OutputKind::LayerHold && bs_[b].out)
          layer_hold_ |= bit;
        else if (out_k.kind == OutputKind::LayerToggle && bs_[b].out_rise)
          layer_toggled_ ^= bit;
      }
    }
    mask = uint8_t((1u | layer_toggled_ | layer_hold_) & layer_bits);
  }
  layer_mask_ = mask;
  const int mouse_layer = MouseLayer(mask);
  if (mouse_layer != mouse_layer_) {
    /* other params: no carry or wheel position leaks across (a weapon wheel
       layer starts at centre) */
    mouse_.Reset();
    mouse_.Push(now, dt_us, mdx, mdy);
    mouse_layer_ = mouse_layer;
  }

  /* ---- 5. outputs ----------------------------------------------------- */
  /* opposing keys per axis, side 0 negative, 1 positive */
  bool side_on[4][2] = {};
  uint64_t side_last[4][2] = {}, side_first[4][2] = {{kNever, kNever}, {kNever, kNever},
                                                      {kNever, kNever}, {kNever, kNever}};
  for (int b = 0; b < binding_count_; ++b) {
    if (!valid_[b] || !bs_[b].out)
      continue;
    const Binding &bd = p.bindings[b];
    for (int k = 0; k < bd.output_count; ++k) {
      const Output &ok = bd.outputs[k];
      if (ok.kind != OutputKind::AxisDirection || ok.axis >= 4 || !ok.value)
        continue;
      const int side = ok.value > 0;
      const uint64_t since = bs_[b].on_since_us;
      side_on[ok.axis][side] = true;
      if (since > side_last[ok.axis][side])
        side_last[ok.axis][side] = since;
      if (since < side_first[ok.axis][side])
        side_first[ok.axis][side] = since;
    }
  }
  const Opposing opposing = p.layers[TopLayer(mask)].opposing;
  bool suppress[4][2] = {};
  for (int a = 0; a < 4; ++a) {
    if (!side_on[a][0] || !side_on[a][1])
      continue;
    int loser = -1; /* -1: both cancel */
    if (opposing == Opposing::FirstWins && side_first[a][0] != side_first[a][1])
      loser = side_first[a][0] < side_first[a][1] ? 1 : 0;
    else if (opposing == Opposing::LastWins && side_last[a][0] != side_last[a][1])
      loser = side_last[a][0] > side_last[a][1] ? 1 : 0;
    suppress[a][0] = loser != 1;
    suppress[a][1] = loser != 0;
  }

  uint32_t buttons = 0;
  float axis[4] = {}, trig[2] = {}, scale[2] = {1.f, 1.f};
  bool want_suspend = false;
  for (int b = 0; b < binding_count_; ++b) {
    if (!valid_[b])
      continue;
    const Binding &bd = p.bindings[b];
    const BindState &s = bs_[b];
    for (int k = 0; k < bd.output_count; ++k) {
      const Output &ok = bd.outputs[k];
      switch (ok.kind) {
      case OutputKind::Button:
        if (s.out)
          buttons |= ok.buttons;
        break;
      case OutputKind::AxisDirection: {
        if (ok.axis >= 4)
          break;
        const bool on = s.out && ok.value && !suppress[ok.axis][ok.value > 0];
        level_[b][k] = analog::RampStep(level_[b][k], on, ok.ramp, dt_ms);
        axis[ok.axis] += Unit(ok.value) * analog::RampShape(level_[b][k], ok.ramp);
        break;
      }
      case OutputKind::Trigger: {
        if (ok.trigger > kTriggerR2)
          break;
        level_[b][k] = analog::RampStep(level_[b][k], s.out, ok.ramp, dt_ms);
        const float v = Clamp(Unit(ok.value), 0.f, 1.f) * analog::RampShape(level_[b][k], ok.ramp);
        if (v > trig[ok.trigger])
          trig[ok.trigger] = v;
        break;
      }
      case OutputKind::StickScale:
        if (s.out && ok.axis < 2) {
          const float f = Unit(ok.value) < 0 ? -Unit(ok.value) : Unit(ok.value);
          if (f < scale[ok.axis])
            scale[ok.axis] = f;
        }
        break;
      case OutputKind::Macro:
        if (s.out_rise)
          StartMacro(b, k, now);
        break;
      case OutputKind::MappingToggle:
        if (s.out_rise)
          want_suspend = true;
        break;
      default: /* None blocks only; layers were done above */
        break;
      }
    }
  }
  if (want_suspend) {
    suspend_mask_ = layer_mask_;
    suspended_ = true;
    ReleaseAll();
    __builtin_memcpy(prev_, down_, kBytes);
    resync_ = false;
    *out = o; /* active = 0 */
    return;
  }

  /* macros: buttons OR, axes set, triggers max */
  float macro_axis[4] = {};
  bool macro_set[4] = {};
  float macro_trig[2] = {};
  for (MacroRun &r : runs_) {
    if (!r.active)
      continue;
    const Macro &m = p.macros[r.macro];
    /* one step per read at most, timed from the read that showed it: every
       step reaches the game, however short */
    const uint64_t dur = uint64_t(p.macro_steps[m.first_step + r.step].duration_ms) * 1000u;
    if (now - r.step_start_us >= dur) {
      r.step_start_us = now;
      if (++r.step >= m.step_count) {
        if (m.repeat_while_held && bs_[r.owner].out)
          r.step = 0;
        else
          r.active = 0;
      }
    }
    if (!r.active)
      continue;
    const MacroStep &st = p.macro_steps[m.first_step + r.step];
    buttons |= st.buttons;
    for (int a = 0; a < 4; ++a)
      if (st.axes[a] != INT16_MIN) {
        macro_axis[a] = Unit(st.axes[a]);
        macro_set[a] = true;
      }
    if (st.l2 / 255.f > macro_trig[0])
      macro_trig[0] = st.l2 / 255.f;
    if (st.r2 / 255.f > macro_trig[1])
      macro_trig[1] = st.r2 / 255.f;
  }

  /* sticks */
  const MouseParams &mp = p.layers[mouse_layer].mouse;
  uint8_t *bytes[2][2] = {{&o.lx, &o.ly}, {&o.rx, &o.ry}};
  for (int st = 0; st < 2; ++st) {
    /* keys: onto the circle first, then scaled, so a diagonal walk is as
       slow as a straight one */
    float kx = axis[2 * st], ky = axis[2 * st + 1];
    analog::ClampCircle(&kx, &ky);
    float x = macro_set[2 * st] ? macro_axis[2 * st] : kx * scale[st];
    float y = macro_set[2 * st + 1] ? macro_axis[2 * st + 1] : ky * scale[st];
    bool mouse = false;
    if (mp.stick == st && mp.mode != MouseMode::Off) {
      float mx, my;
      mouse = mouse_.Evaluate(mp, now, dt_ms, mdx, mdy, scale[st], &mx, &my);
      if (mouse) {
        x += mx;
        y += my;
      }
    }
    analog::ClampCircle(&x, &y);
    if (!mouse)
      dither_[st][0] = dither_[st][1] = 0.f;
    *bytes[st][0] = analog::Quantise(x, mouse ? &dither_[st][0] : nullptr);
    *bytes[st][1] = analog::Quantise(y, mouse ? &dither_[st][1] : nullptr);
  }

  /* triggers: a plain L2/R2 button means a full pull; the bit follows the
     value past the threshold, since games read either */
  const uint32_t trigger_bit[2] = {kPadL2, kPadR2};
  const int threshold = p.trigger_button_threshold ? p.trigger_button_threshold : 1;
  uint8_t *trigger_byte[2] = {&o.l2, &o.r2};
  for (int t = 0; t < 2; ++t) {
    float v = trig[t] > macro_trig[t] ? trig[t] : macro_trig[t];
    if (buttons & trigger_bit[t])
      v = 1.f;
    *trigger_byte[t] = Byte(v);
    if (*trigger_byte[t] && *trigger_byte[t] >= threshold)
      buttons |= trigger_bit[t];
  }

  o.buttons = buttons & ~uint32_t(kPadIntercepted);
  /* 0 when nothing is ours to add: the real pad passes untouched */
  o.active = (o.buttons || o.lx != 128 || o.ly != 128 || o.rx != 128 ||
              o.ry != 128 || o.l2 || o.r2)
                 ? 1
                 : 0;
  __builtin_memcpy(prev_, down_, kBytes);
  *out = o;
}

} // namespace onion::kbm
