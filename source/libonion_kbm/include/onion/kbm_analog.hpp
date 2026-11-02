/* Keyboard & mouse for games: the analog side of the engine.
 *
 * Ramps for keys that drive sticks and triggers, the stick circle and byte
 * quantising, and the mouse-to-stick translator. Plain float maths with our
 * own pow/sqrt, so the agent needs no libm and the host tests get the same
 * numbers bit for bit. Every value that leaves here is finite.
 */
#pragma once

#include <onion/kbm_types.hpp>

namespace onion::kbm::analog {

float Clamp(float x, float lo, float hi); /* NaN: lo */

/* One analog output's 0..1 level: rises toward 1 while `on`, falls toward 0
   after, at the ramp's speed. 0 ms is instant. */
float RampStep(float level, bool on, const Ramp &ramp, float dt_ms);
/* The level through the ramp's curve (curve_x100: 100 linear). */
float RampShape(float level, const Ramp &ramp);

/* Keep (x, y) inside the unit circle, direction unchanged. */
void ClampCircle(float *x, float *y);
/* -1..1 to a stick byte (128 centre, 1..255). With `err`, the rounding error
   is carried to the next call (finer average for slow aim). */
uint8_t Quantise(float d, float *err);

/* Mouse speed (velocity mode) or mouse travel (position mode) to one stick.
   Fed once per pad read with the motion since the previous read. */
class MouseStick {
public:
  /* Full deflection at sensitivity 100 is 1 / kSensBase counts/ms
     (16.7 c/ms, about 26 cm/s at 1600 CPI). */
  static constexpr float kSensBase = 0.06f;
  /* Saturation carry cap, in ms at full deflection. */
  static constexpr float kCarryCapMs = 150.f;

  void Reset();
  /* Records this read's motion, spread over the `dt_us` since the last read. */
  void Push(uint64_t now_us, uint32_t dt_us, int32_t dx, int32_t dy);
  /* Average speed over the last `window_ms`, counts/ms, raw axes (x right,
     y down). Ramps to 0 within the window after the mouse stops. */
  void Velocity(uint64_t now_us, uint16_t window_ms, float *vx, float *vy) const;
  /* This read's stick deflection (-1..1 each, before the circle clamp).
     `scale` is the StickScale of the stick (1 = none). False: nothing to add. */
  bool Evaluate(const MouseParams &p, uint64_t now_us, float dt_ms, int32_t dx,
                int32_t dy, float scale, float *x, float *y);

private:
  struct Motion {
    uint64_t end_us;
    uint32_t dt_us;
    int32_t dx, dy;
  };
  /* Reads less than 1 ms apart share an entry, so 256 entries always hold
     more than the longest (100 ms) window, at any read rate. */
  static constexpr int kRing = 256;
  static constexpr uint32_t kMergeUs = 1000;
  Motion ring_[kRing];
  int head_ = 0, count_ = 0;
  uint64_t last_motion_us_ = 0;
  float carry_ = 0.f, carry_x_ = 0.f, carry_y_ = 0.f;
  float px_ = 0.f, py_ = 0.f;
};

} // namespace onion::kbm::analog
