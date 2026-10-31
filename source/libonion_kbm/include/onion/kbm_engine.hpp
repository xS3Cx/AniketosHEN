/* Keyboard & mouse for games: the runtime engine.
 *
 * Turns one snapshot of keyboard and mouse input into a controller state,
 * once per game pad read. Fixed-size state, no heap, no exceptions, no locks,
 * no logging; the same inputs at the same times give the same outputs.
 *
 * Per Tick, in order:
 *  1. wheel notches become short presses of their input (queued, at most 3
 *     pending), mouse speed becomes the kMouseMove* inputs;
 *  2. the suspend key (and any MappingToggle input while suspended) toggles
 *     the mapping; suspended means active = 0 and everything released;
 *  3. a pressed input picks its bindings once, at the press: the highest
 *     active layer that has a binding for it whose modifiers are held, and
 *     there the bindings with the most modifiers (chords). It keeps them until
 *     release, whatever happens to the layers. A None binding is a binding,
 *     so it stops the fall-through. Inputs that drive layers resolve first,
 *     so Shift-layer + W pressed in the same read gives the layer's W;
 *  4. gestures (Press, Tap, LongPress, DoubleTap, Release) then modes (Hold,
 *     Toggle, Turbo, Pulse) give each binding's on/off;
 *  5. outputs: buttons OR; stick directions summed per axis after opposing
 *     keys are resolved (the losing side ramps out), each through its ramp;
 *     triggers max, with the L2/R2 bit past the profile threshold; StickScale
 *     min per stick; macros; the mouse adds to its stick; each stick clamped
 *     to the circle.
 */
#pragma once

#include <onion/kbm_analog.hpp>
#include <onion/kbm_types.hpp>

namespace onion::kbm {

/* One snapshot of the devices. Motion and notches are since the previous
   frame: hand each frame to Tick once. */
struct InputFrame {
  uint64_t now_us; /* monotonic; a step back counts as no time passing */
  uint8_t down[(kInputCount + 7) / 8]; /* bit i: input i held (wheel and
                                          kMouseMove* bits are ignored) */
  int16_t wheel_v, wheel_h; /* notches: v > 0 up, h > 0 right */
  int32_t mouse_dx, mouse_dy; /* counts: x right, y down */
};

class Engine {
public:
  Engine() { SetProfile(nullptr); }

  /* The profile must stay valid and unchanged until the next SetProfile
     (copy it out of the control page first). nullptr, a bad magic, version
     or size: inactive. Releases everything. */
  void SetProfile(const ProfileBlob *profile);
  /* Releases everything (buttons, sticks, toggles, layers, macros, wheel
     queue); keys still held count again only after a new press. The
     suspended state is the user's and stays. */
  void Reset();
  void Tick(const InputFrame &in, PadState *out);

  bool Suspended() const { return suspended_; }
  uint8_t ActiveLayers() const { return layer_mask_; }

private:
  struct InputState {
    uint64_t press_us, release_us;
    uint64_t held_us;  /* length of the last finished press */
    uint32_t defer_us; /* Tap waits this long for a DoubleTap */
    uint8_t was_double; /* this press is the second of a double tap */
  };
  struct BindState {
    uint64_t pulse_end_us;    /* Tap / Release gesture pulse */
    uint64_t tap_deadline_us; /* Tap deferred for a possible DoubleTap */
    uint64_t mode_end_us;     /* Pulse mode */
    uint64_t turbo_end_us;    /* end of the current Turbo phase */
    uint64_t on_since_us;     /* opposing keys: who came last */
    uint8_t armed, dbl, firing, latch, out, out_rise;
    uint8_t pulse_pending, mode_pending; /* queued pulses, each its own press */
    uint8_t turbo_on;
  };
  struct MacroRun {
    uint64_t step_start_us;
    uint16_t owner, step;
    uint8_t output, macro, active;
  };
  static constexpr int kMaxRunning = 8;
  static constexpr int kWheelInputs = 4;
  static constexpr int kWheelQueueCap = 3;

  void ReleaseAll();
  void OnPress(int input, uint8_t mask, uint64_t now);
  void EvalBinding(int b, uint64_t now);
  void StartMacro(int b, int output, uint64_t now);
  uint8_t TopLayer(uint8_t mask) const;
  int MouseLayer(uint8_t mask) const;
  bool Down(int i) const { return down_[i >> 3] & (1u << (i & 7)); }
  bool Pressed(int i) const { return pressed_[i >> 3] & (1u << (i & 7)); }
  bool Released(int i) const { return released_[i >> 3] & (1u << (i & 7)); }

  const ProfileBlob *p_ = nullptr;
  uint16_t binding_count_ = 0;
  uint8_t layer_count_ = 1;

  /* bindings grouped by input (built once in SetProfile) */
  uint16_t first_[kInputCount];
  uint16_t count_[kInputCount];
  uint16_t order_[kMaxBindings];
  uint8_t valid_[kMaxBindings];
  uint8_t mod_count_[kMaxBindings];
  uint8_t has_layer_out_[kMaxBindings];
  uint8_t has_mtoggle_[kMaxBindings];
  uint8_t input_layer_[(kInputCount + 7) / 8];  /* input drives a layer */
  uint8_t input_mtoggle_[(kInputCount + 7) / 8]; /* input has MappingToggle */
  uint16_t wheel_pulse_ms_[kWheelInputs];

  /* runtime */
  uint64_t now_us_ = 0;
  bool have_time_ = false;
  bool resync_ = true;
  bool suspended_ = false;
  uint8_t down_[(kInputCount + 7) / 8];
  uint8_t prev_[(kInputCount + 7) / 8];
  uint8_t pressed_[(kInputCount + 7) / 8];
  uint8_t released_[(kInputCount + 7) / 8];
  InputState in_[kInputCount];
  BindState bs_[kMaxBindings];
  float level_[kMaxBindings][kMaxOutputsPerBinding];
  MacroRun runs_[kMaxRunning];
  uint8_t layer_toggled_ = 0, layer_hold_ = 0, layer_mask_ = 1;
  uint8_t suspend_mask_ = 1; /* layers when suspended: where MappingToggle resolves */
  uint8_t wheel_queue_[kWheelInputs];
  uint8_t wheel_on_[kWheelInputs];
  uint64_t wheel_end_us_[kWheelInputs];
  uint8_t move_on_[4];
  int mouse_layer_ = -1;
  analog::MouseStick mouse_;
  float dither_[2][2];
};

} // namespace onion::kbm
