/* Keyboard & mouse for games: ramps, stick shaping and mouse to stick.
 * Runs inside the game's pad read hook: no heap, no libm, no locks. */
#include <onion/kbm_analog.hpp>

namespace onion::kbm::analog {
namespace {

uint32_t Bits(float f) {
  uint32_t u;
  __builtin_memcpy(&u, &f, sizeof u);
  return u;
}

float FromBits(uint32_t u) {
  float f;
  __builtin_memcpy(&f, &u, sizeof f);
  return f;
}

bool IsFinite(float x) { return x == x && x - x == 0.f; }

/* x > 0 and finite. Mantissa in [1, 2) through the atanh series: ~1e-7. */
float Log2(float x) {
  const uint32_t u = Bits(x);
  const int e = int((u >> 23) & 0xff);
  if (e == 0)
    return -126.f; /* denormal: small enough to be zero downstream */
  const float m = FromBits((u & 0x007fffffu) | 0x3f800000u);
  const float s = (m - 1.f) / (m + 1.f), s2 = s * s;
  const float series =
      s * (1.f + s2 * (1.f / 3 + s2 * (1.f / 5 + s2 * (1.f / 7 + s2 * (1.f / 9)))));
  return float(e - 127) + series * 2.885390081777927f; /* 2 / ln 2 */
}

float Exp2(float y) {
  if (y < -126.f)
    return 0.f;
  if (y > 127.f)
    y = 127.f;
  int i = int(y);
  if (float(i) > y)
    --i;
  const float f = (y - float(i)) * 0.6931471805599453f; /* [0, ln 2) */
  float term = 1.f, sum = 1.f;
  for (int k = 1; k <= 8; ++k) {
    term *= f / float(k);
    sum += term;
  }
  return sum * FromBits(uint32_t(i + 127) << 23);
}

} // namespace

float Finite(float x) { return IsFinite(x) ? x : 0.f; }

float Clamp(float x, float lo, float hi) {
  if (!(x >= lo))
    return lo; /* also NaN */
  return x > hi ? hi : x;
}

float Pow(float base, float e) {
  if (!(base > 0.f) || !IsFinite(base) || !IsFinite(e))
    return 0.f;
  if (base == 1.f || e == 0.f)
    return 1.f;
  if (e == 1.f)
    return base;
  return Exp2(e * Log2(base));
}

float Sqrt(float x) {
  if (!(x > 0.f) || !IsFinite(x))
    return 0.f;
  float r = Exp2(0.5f * Log2(x));
  r = 0.5f * (r + x / r);
  return 0.5f * (r + x / r);
}

float RampStep(float level, bool on, const Ramp &ramp, float dt_ms) {
  level = Clamp(level, 0.f, 1.f);
  if (on)
    level = ramp.rise_ms ? level + dt_ms / float(ramp.rise_ms) : 1.f;
  else
    level = ramp.fall_ms ? level - dt_ms / float(ramp.fall_ms) : 0.f;
  return Clamp(level, 0.f, 1.f);
}

float RampShape(float level, const Ramp &ramp) {
  if (!(level > 0.f))
    return 0.f;
  if (level >= 1.f)
    return 1.f;
  const float curve = ramp.curve_x100 ? Clamp(ramp.curve_x100 / 100.f, 0.1f, 10.f) : 1.f;
  return Clamp(Pow(level, curve), 0.f, 1.f);
}

void ClampCircle(float *x, float *y) {
  *x = Finite(*x);
  *y = Finite(*y);
  const float r2 = *x * *x + *y * *y;
  if (r2 > 1.f) {
    const float r = Sqrt(r2);
    *x /= r;
    *y /= r;
  }
}

uint8_t Quantise(float d, float *err) {
  float f = 128.f + Clamp(d, -1.f, 1.f) * 127.f;
  if (err)
    f += Clamp(*err, -1.f, 1.f);
  int b = int(f + 0.5f); /* f >= 0: truncation is floor */
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  if (err)
    *err = Clamp(f - float(b), -1.f, 1.f);
  return uint8_t(b);
}

void CompensateDeadzone(const MouseParams &p, float ux, float uy, float m,
                        float *x, float *y) {
  const float dz = Clamp(p.deadzone_x1000 / 1000.f, 0.f, 0.5f);
  const float outer = p.outer_x1000 ? Clamp(p.outer_x1000 / 1000.f, dz, 1.f) : 1.f;
  m = Clamp(m, 0.f, 1.f);
  if (m <= 0.f) {
    *x = *y = 0.f;
    return;
  }
  if (p.deadzone_shape == DeadzoneShape::Square) {
    float a[2] = {ux * m, uy * m};
    for (float &v : a) {
      const float mag = v < 0 ? -v : v;
      /* components under half a byte step stay 0, or near-axis aim snaps */
      v = mag > 0.5f / 127.f ? (v < 0 ? -1.f : 1.f) * (dz + (outer - dz) * mag) : 0.f;
    }
    *x = a[0];
    *y = a[1];
    return;
  }
  const float r = dz + (outer - dz) * m;
  *x = ux * r;
  *y = uy * r;
}

void MouseStick::Reset() {
  head_ = count_ = 0;
  last_motion_us_ = 0;
  carry_ = carry_x_ = carry_y_ = 0.f;
  px_ = py_ = 0.f;
}

void MouseStick::Push(uint64_t now_us, uint32_t dt_us, int32_t dx, int32_t dy) {
  if (!dx && !dy)
    return;
  if (count_) {
    Motion &last = ring_[(head_ - 1 + kRing) % kRing];
    const uint64_t start = last.end_us - last.dt_us;
    if (now_us - start < kMergeUs) {
      last.dx += dx;
      last.dy += dy;
      last.dt_us = uint32_t(now_us - start);
      last.end_us = now_us;
      last_motion_us_ = now_us;
      return;
    }
  }
  ring_[head_] = {now_us, dt_us ? dt_us : 1u, dx, dy};
  head_ = (head_ + 1) % kRing;
  if (count_ < kRing)
    ++count_;
  last_motion_us_ = now_us;
}

void MouseStick::Velocity(uint64_t now_us, uint16_t window_ms, float *vx, float *vy) const {
  const int64_t w = int64_t(window_ms < 4 ? 4 : window_ms > 100 ? 100 : window_ms) * 1000;
  const int64_t now = int64_t(now_us), from = now - w;
  float sx = 0.f, sy = 0.f;
  for (int n = 0; n < count_; ++n) {
    const Motion &e = ring_[(head_ - 1 - n + kRing) % kRing];
    const int64_t end = int64_t(e.end_us) < now ? int64_t(e.end_us) : now;
    if (end <= from)
      break; /* the ring is in time order */
    const int64_t start = end - int64_t(e.dt_us);
    const int64_t overlap = end - (start > from ? start : from);
    const float share = float(overlap) / float(e.dt_us);
    sx += float(e.dx) * share;
    sy += float(e.dy) * share;
  }
  *vx = Finite(sx * 1000.f / float(w));
  *vy = Finite(sy * 1000.f / float(w));
}

bool MouseStick::Evaluate(const MouseParams &p, uint64_t now_us, float dt_ms,
                          int32_t dx, int32_t dy, float scale, float *x, float *y) {
  *x = *y = 0.f;
  const float inv_x = (p.invert & 1) ? -1.f : 1.f;
  const float inv_y = (p.invert & 2) ? -1.f : 1.f;
  const float yx = p.yx_ratio_x100 / 100.f;
  const float gamma = p.exponent_x100 ? Clamp(p.exponent_x100 / 100.f, 0.1f, 10.f) : 1.f;
  scale = Clamp(scale, 0.f, 1.f);

  if (p.mode == MouseMode::Position) {
    /* shortcut: no sensitivity here, position_range alone sets the gain */
    const float k = 1.f / float(p.position_range ? p.position_range : 1000);
    px_ = Finite(px_ + float(dx) * k * inv_x);
    py_ = Finite(py_ + float(dy) * k * yx * inv_y);
    float r = Sqrt(px_ * px_ + py_ * py_);
    if (p.spring_per_s_x10 && r > 0.f) {
      const float nr = Clamp(r - p.spring_per_s_x10 / 10.f * dt_ms / 1000.f, 0.f, 1.f);
      px_ *= nr / r;
      py_ *= nr / r;
      r = nr;
    }
    if (r > 1.f) { /* stays on the rim: a wheel selection holds without moving */
      px_ /= r;
      py_ /= r;
      r = 1.f;
    }
    if (!(r > 0.f)) {
      px_ = py_ = 0.f;
      return false;
    }
    CompensateDeadzone(p, px_ / r, py_ / r, Pow(r, gamma) * scale, x, y);
    return *x != 0.f || *y != 0.f;
  }
  if (p.mode != MouseMode::Velocity)
    return false;

  float vx, vy;
  Velocity(now_us, p.smoothing_ms, &vx, &vy);
  vx *= inv_x;
  vy *= yx * inv_y;
  const float speed = Sqrt(vx * vx + vy * vy);
  float ux = speed > 0.f ? vx / speed : 0.f, uy = speed > 0.f ? vy / speed : 0.f;
  float t = Pow(speed * (p.sensitivity_x100 / 100.f) * kSensBase, gamma);

  /* Flick boost: up to boost x the gain as the move nears saturation, and
     the excess past full deflection is owed (carry) instead of lost. */
  const float boost = Clamp(p.boost_x100 / 100.f, 1.f, 5.f);
  if (boost > 1.f) {
    const float k = Clamp((t - 0.5f) / 0.5f, 0.f, 1.f);
    t *= 1.f + (boost - 1.f) * k;
  }
  t *= scale;

  if (boost > 1.f && dt_ms > 0.f) {
    if (t > 1.f) {
      carry_ = Clamp(carry_ + (t - 1.f) * dt_ms, 0.f, kCarryCapMs);
      carry_x_ = ux;
      carry_y_ = uy;
      t = 1.f;
    } else if (carry_ > 0.f) {
      const bool reversed = speed > 0.f && ux * carry_x_ + uy * carry_y_ < 0.f;
      const bool stale = speed == 0.f && now_us - last_motion_us_ > 200000;
      if (reversed || stale) {
        carry_ = 0.f;
      } else {
        const float extra = Clamp(carry_ / dt_ms, 0.f, 1.f - t);
        t += extra;
        carry_ = Clamp(carry_ - extra * dt_ms, 0.f, kCarryCapMs);
        if (speed == 0.f) { /* finish the flick after the hand stopped */
          ux = carry_x_;
          uy = carry_y_;
        }
      }
    }
  } else if (boost <= 1.f) {
    carry_ = 0.f;
  }

  t = Clamp(t, 0.f, 1.f);
  if (t <= 0.f)
    return false;
  CompensateDeadzone(p, ux, uy, t, x, y);
  return *x != 0.f || *y != 0.f;
}

} // namespace onion::kbm::analog
