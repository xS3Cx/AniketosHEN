#pragma once

/* A small software renderer for the PS3 wave background: float (HDR) RGBA
 * images, bilinear textures, a triangle rasteriser with per-vertex
 * attributes and a fragment functor (so Sony's RSX programs can be ported
 * one to one), separable Gaussian blurs for the glare and the final tone
 * mapping to 8-bit pixels. No dependencies beyond the C++ standard library;
 * the same header runs on the host for the tests under source/util/tests
 * and on the console in ShellUI. */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AniketosTheme::Soft {

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
};

inline Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline Vec4 operator*(Vec4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline Vec4 operator*(Vec4 a, Vec4 b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
inline float dot3(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot4(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }
inline Vec4 normalize3(Vec4 v) {
  const float len = std::sqrt(dot3(v, v));
  return len > 1e-12f ? v * (1.0f / len) : Vec4{};
}

/* A 4x4 matrix in row-major order; transforms column vectors (m * v). */
struct Mat4 {
  float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

  Vec4 row(int r) const { return {m[r][0], m[r][1], m[r][2], m[r][3]}; }
  Vec4 operator*(Vec4 v) const {
    return {dot4(row(0), v), dot4(row(1), v), dot4(row(2), v), dot4(row(3), v)};
  }
  Mat4 operator*(const Mat4 &o) const {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] + m[i][3] * o.m[3][j];
    return r;
  }
  static Mat4 perspective(float fovy_degrees, float aspect, float near, float far) {
    const float f = 1.0f / std::tan(fovy_degrees * 0.5f * 3.14159265f / 180.0f);
    Mat4 r{};
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][2] = (far + near) / (near - far);
    r.m[2][3] = 2.0f * far * near / (near - far);
    r.m[3][2] = -1.0f;
    r.m[3][3] = 0.0f;
    return r;
  }
  static Mat4 translation(float x, float y, float z) {
    Mat4 r;
    r.m[0][3] = x;
    r.m[1][3] = y;
    r.m[2][3] = z;
    return r;
  }
  /* rotation of `degrees` about the unit axis (x, y, z) */
  static Mat4 rotation(float degrees, float x, float y, float z) {
    const float a = degrees * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a), t = 1.0f - c;
    Mat4 r;
    r.m[0][0] = t * x * x + c;
    r.m[0][1] = t * x * y - s * z;
    r.m[0][2] = t * x * z + s * y;
    r.m[1][0] = t * x * y + s * z;
    r.m[1][1] = t * y * y + c;
    r.m[1][2] = t * y * z - s * x;
    r.m[2][0] = t * x * z - s * y;
    r.m[2][1] = t * y * z + s * x;
    r.m[2][2] = t * z * z + c;
    return r;
  }
};

/* A float RGBA image. Row-major, top row first. */
struct Image {
  int width = 0, height = 0;
  std::vector<Vec4> pixels;

  Image() = default;
  Image(int w, int h) { reset(w, h); }
  void reset(int w, int h) {
    width = w;
    height = h;
    pixels.assign(static_cast<size_t>(w) * h, Vec4{});
  }
  void fill(Vec4 c) { std::fill(pixels.begin(), pixels.end(), c); }
  Vec4 &at(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
  const Vec4 &at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }

  /* bilinear sample with normalised coordinates; wraps or clamps */
  Vec4 sample(float u, float v, bool wrap = true) const {
    if (width <= 0 || height <= 0)
      return {};
    float fx = u * width - 0.5f, fy = v * height - 0.5f;
    if (wrap) {
      fx -= std::floor(fx / width) * width;
      fy -= std::floor(fy / height) * height;
    } else {
      fx = std::min(std::max(fx, 0.0f), width - 1.0f);
      fy = std::min(std::max(fy, 0.0f), height - 1.0f);
    }
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - x0, ty = fy - y0;
    auto texel = [&](int x, int y) -> const Vec4 & {
      if (wrap) {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
      } else {
        x = std::min(std::max(x, 0), width - 1);
        y = std::min(std::max(y, 0), height - 1);
      }
      return at(x, y);
    };
    return lerp(lerp(texel(x0, y0), texel(x0 + 1, y0), tx), lerp(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx), ty);
  }
  /* a 1-D lookup table (a 512x1 texture, say) */
  Vec4 lookup(float u) const { return sample(u, 0.5f, false); }
};

/* Blend of a fragment onto the target. */
enum class Blend { Replace, Alpha, Additive, Multiply };

inline void blend_pixel(Vec4 &dst, Vec4 src, Blend mode) {
  switch (mode) {
  case Blend::Replace: dst = src; break;
  case Blend::Alpha: dst = lerp(dst, src, clamp01(src.w)); dst.w = std::max(dst.w, src.w); break;
  case Blend::Additive: dst = dst + src * src.w; break;
  case Blend::Multiply: dst = dst * src; break;
  }
}

constexpr int kMaxAttributes = 8;

/* A vertex after the vertex program: clip-space position and up to
   kMaxAttributes interpolated vectors (texture coordinates, colours). */
struct Vertex {
  Vec4 clip;
  Vec4 attr[kMaxAttributes];
};

/* Rasterises one triangle into `target`, calling fragment(x, y, attrs) for
   each covered pixel centre; the functor returns the fragment colour (or
   sets `discard`). Attributes are perspective-correct. Culling: none. */
template <typename Fragment>
void draw_triangle(Image &target, const Vertex &a, const Vertex &b, const Vertex &c, int attribute_count,
                   Blend blend, Fragment &&fragment) {
  const Vertex *v[3] = {&a, &b, &c};
  float sx[3], sy[3], iw[3];
  for (int i = 0; i < 3; ++i) {
    const Vec4 &p = v[i]->clip;
    if (p.w <= 1e-6f)
      return; /* behind the eye: the ribbons never cross the near plane */
    iw[i] = 1.0f / p.w;
    sx[i] = (p.x * iw[i] * 0.5f + 0.5f) * target.width;
    sy[i] = (0.5f - p.y * iw[i] * 0.5f) * target.height;
  }
  const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
  if (std::fabs(area) < 1e-9f)
    return;
  const float inv_area = 1.0f / area;
  const int min_x = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
  const int max_x = std::min(target.width - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
  const int min_y = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
  const int max_y = std::min(target.height - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
  if (min_x > max_x || min_y > max_y)
    return;
  Vec4 attrs[kMaxAttributes];
  for (int y = min_y; y <= max_y; ++y) {
    const float py = y + 0.5f;
    for (int x = min_x; x <= max_x; ++x) {
      const float px = x + 0.5f;
      float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv_area;
      float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv_area;
      float w2 = 1.0f - w0 - w1;
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
        continue;
      /* perspective-correct weights */
      const float q0 = w0 * iw[0], q1 = w1 * iw[1], q2 = w2 * iw[2];
      const float qs = q0 + q1 + q2;
      if (qs <= 0.0f)
        continue;
      const float n0 = q0 / qs, n1 = q1 / qs, n2 = q2 / qs;
      for (int k = 0; k < attribute_count; ++k)
        attrs[k] = v[0]->attr[k] * n0 + v[1]->attr[k] * n1 + v[2]->attr[k] * n2;
      bool discard = false;
      const Vec4 colour = fragment(x, y, attrs, discard);
      if (!discard)
        blend_pixel(target.at(x, y), colour, blend);
    }
  }
}

/* A full-screen pass: fragment(x, y, u, v) for every pixel, u/v in 0..1. */
template <typename Fragment>
void draw_fullscreen(Image &target, Blend blend, Fragment &&fragment) {
  for (int y = 0; y < target.height; ++y) {
    const float v = (y + 0.5f) / target.height;
    for (int x = 0; x < target.width; ++x) {
      const float u = (x + 0.5f) / target.width;
      blend_pixel(target.at(x, y), fragment(x, y, u, v), blend);
    }
  }
}

/* Box downsample by 2 (for the glare pyramid). */
inline void downsample2(const Image &src, Image &dst) {
  dst.reset(std::max(1, src.width / 2), std::max(1, src.height / 2));
  for (int y = 0; y < dst.height; ++y)
    for (int x = 0; x < dst.width; ++x) {
      const int sx = std::min(x * 2, src.width - 1), sy = std::min(y * 2, src.height - 1);
      const int sx1 = std::min(sx + 1, src.width - 1), sy1 = std::min(sy + 1, src.height - 1);
      dst.at(x, y) = (src.at(sx, sy) + src.at(sx1, sy) + src.at(sx, sy1) + src.at(sx1, sy1)) * 0.25f;
    }
}

/* Separable Gaussian blur with the given sigma in pixels; `scratch` is
   reused between calls. Edges clamp. */
inline void gaussian_blur(Image &image, float sigma, Image &scratch) {
  if (sigma <= 0.01f || image.width == 0)
    return;
  const int radius = std::min(64, std::max(1, static_cast<int>(std::ceil(sigma * 3.0f))));
  std::vector<float> kernel(static_cast<size_t>(radius) + 1);
  float sum = 0.0f;
  for (int i = 0; i <= radius; ++i) {
    kernel[i] = std::exp(-0.5f * (i * i) / (sigma * sigma));
    sum += i ? 2.0f * kernel[i] : kernel[i];
  }
  for (float &k : kernel)
    k /= sum;
  scratch.reset(image.width, image.height);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      Vec4 acc = image.at(x, y) * kernel[0];
      for (int i = 1; i <= radius; ++i)
        acc = acc + (image.at(std::max(0, x - i), y) + image.at(std::min(image.width - 1, x + i), y)) * kernel[i];
      scratch.at(x, y) = acc;
    }
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      Vec4 acc = scratch.at(x, y) * kernel[0];
      for (int i = 1; i <= radius; ++i)
        acc = acc + (scratch.at(x, std::max(0, y - i)) + scratch.at(x, std::min(image.height - 1, y + i))) * kernel[i];
      image.at(x, y) = acc;
    }
}

/* Bilinear upsample of `src` added onto `dst` with weight. */
inline void add_upsampled(Image &dst, const Image &src, float weight) {
  for (int y = 0; y < dst.height; ++y)
    for (int x = 0; x < dst.width; ++x)
      dst.at(x, y) = dst.at(x, y) + src.sample((x + 0.5f) / dst.width, (y + 0.5f) / dst.height, false) * weight;
}

/* Packs a float image into 8-bit RGBA (row-major, top first) with a gamma
   of 1/`gamma`; HDR values clip at 1. */
inline void pack_rgba8(const Image &src, uint8_t *out, float gamma = 1.0f) {
  const float inv_gamma = 1.0f / gamma;
  size_t o = 0;
  for (const Vec4 &p : src.pixels) {
    const float c[4] = {p.x, p.y, p.z, p.w};
    for (int k = 0; k < 4; ++k) {
      float v = clamp01(c[k]);
      if (k < 3 && gamma != 1.0f)
        v = std::pow(v, inv_gamma);
      out[o++] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
  }
}

} // namespace AniketosTheme::Soft
