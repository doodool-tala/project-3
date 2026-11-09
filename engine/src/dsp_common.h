// Shared low-level DSP primitives for the B-3 engine.
// Portable C++17: no platform, STL-allocator or third-party dependencies beyond
// the standard library, so the exact same code runs on Android and in the
// host-side test/bench harness.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace b3 {
namespace dsp {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

// Cheap, deterministic PRNG. Used for wheel trims, leakage phases, contact
// noise and rotor wind; every seed is fixed so an organ sounds the same after
// a restart.
class Rng {
 public:
  explicit Rng(uint32_t seed = 0x243F6A88u) : s_(seed ? seed : 0x9E3779B9u) {}
  inline uint32_t next() {
    uint32_t x = s_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_ = x;
    return x;
  }
  inline float uni() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
  inline float bipolar() { return uni() * 2.0f - 1.0f; }

 private:
  uint32_t s_;
};

// RBJ biquad, transposed direct form II. All filters in the engine are 32-bit
// float; the state is kept in float to stay cheap on ARM NEON.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;

  inline void reset() { z1 = z2 = 0; }

  inline float process(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  inline void setCoeffs(float nb0, float nb1, float nb2, float na1, float na2) {
    b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
  }

  void lowpass(double sr, double f, double q) {
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    setCoeffs(static_cast<float>((1.0 - cw) * 0.5 / a0),
              static_cast<float>((1.0 - cw) / a0),
              static_cast<float>((1.0 - cw) * 0.5 / a0),
              static_cast<float>(-2.0 * cw / a0),
              static_cast<float>((1.0 - alpha) / a0));
  }

  void highpass(double sr, double f, double q) {
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    setCoeffs(static_cast<float>((1.0 + cw) * 0.5 / a0),
              static_cast<float>(-(1.0 + cw) / a0),
              static_cast<float>((1.0 + cw) * 0.5 / a0),
              static_cast<float>(-2.0 * cw / a0),
              static_cast<float>((1.0 - alpha) / a0));
  }

  void bandpass(double sr, double f, double q) {
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    setCoeffs(static_cast<float>(alpha / a0), 0.0f,
              static_cast<float>(-alpha / a0),
              static_cast<float>(-2.0 * cw / a0),
              static_cast<float>((1.0 - alpha) / a0));
  }

  void peak(double sr, double f, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    setCoeffs(static_cast<float>((1.0 + alpha * A) / a0),
              static_cast<float>(-2.0 * cw / a0),
              static_cast<float>((1.0 - alpha * A) / a0),
              static_cast<float>(-2.0 * cw / a0),
              static_cast<float>((1.0 - alpha / A) / a0));
  }

  void lowShelf(double sr, double f, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double beta = std::sqrt(A) / 0.7071067811865476;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + beta * sw;
    setCoeffs(static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cw + beta * sw) / a0),
              static_cast<float>(2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0),
              static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cw - beta * sw) / a0),
              static_cast<float>(-2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0),
              static_cast<float>(((A + 1.0) + (A - 1.0) * cw - beta * sw) / a0));
  }

  void highShelf(double sr, double f, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double beta = std::sqrt(A) / 0.7071067811865476;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + beta * sw;
    setCoeffs(static_cast<float>(A * ((A + 1.0) + (A - 1.0) * cw + beta * sw) / a0),
              static_cast<float>(-2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0),
              static_cast<float>(A * ((A + 1.0) + (A - 1.0) * cw - beta * sw) / a0),
              static_cast<float>(2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0),
              static_cast<float>(((A + 1.0) - (A - 1.0) * cw - beta * sw) / a0));
  }
};

// One-pole smoother. `setTimeMs` converts a time constant into a per-sample
// coefficient; used for gains, delays and filter modulation.
struct Smoothed {
  float value = 0.0f;
  float target = 0.0f;
  float coef = 0.01f;

  void setTimeMs(double sr, double ms) {
    const double samples = std::max(1.0, 0.001 * ms * sr);
    coef = static_cast<float>(1.0 - std::exp(-1.0 / samples));
  }
  inline float next() {
    value += (target - value) * coef;
    return value;
  }
  inline void snap(float v) { value = target = v; }
};

// Circular delay line with Catmull-Rom fractional read. Delay is measured
// backwards from the most recently written sample.
class DelayLine {
 public:
  void resize(int maxSamples) {
    int n = 8;
    while (n < maxSamples) n <<= 1;
    buf_.assign(static_cast<size_t>(n), 0.0f);
    size_ = n;
    mask_ = n - 1;
    write_ = 0;
  }
  void clear() {
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    write_ = 0;
  }
  inline void write(float x) {
    buf_[static_cast<size_t>(write_)] = x;
    write_ = (write_ + 1) & mask_;
  }
  inline float readCubic(float delaySamples) const {
    float d = delaySamples;
    if (d < 1.0f) d = 1.0f;
    const float maxD = static_cast<float>(size_ - 3);
    if (d > maxD) d = maxD;
    const float fidx = static_cast<float>(write_ - 1) - d;
    const int i = static_cast<int>(std::floor(fidx));
    const float frac = fidx - static_cast<float>(i);
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    // Catmull-Rom (a = -0.5).
    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * frac + a1) * frac + a2) * frac + y1;
  }
  inline float readLinear(float delaySamples) const {
    if (delaySamples < 0.0f) delaySamples = 0.0f;
    const float fidx = static_cast<float>(write_ - 1) - delaySamples;
    const int i = static_cast<int>(std::floor(fidx));
    const float frac = fidx - static_cast<float>(i);
    return at(i) + (at(i + 1) - at(i)) * frac;
  }
  inline int size() const { return size_; }

 private:
  inline float at(int k) const {
    return buf_[static_cast<size_t>(k) & static_cast<size_t>(mask_)];
  }
  std::vector<float> buf_;
  int size_ = 0;
  int mask_ = 0;
  int write_ = 0;
};

// 2^12-entry wavetable oscillator. Phase is 32-bit fixed point, which keeps
// 91 free-running tonewheels from drifting due to float rounding.
class TableOsc {
 public:
  static constexpr int kTableBits = 12;
  static constexpr int kTableSize = 1 << kTableBits;

  void setTable(const float* t) { table_ = t; }
  void setFrequency(double sr, double hz) {
    const double inc = hz / sr * 4294967296.0;
    inc_ = static_cast<uint32_t>(static_cast<int64_t>(inc));
  }
  void setPhase(float phase01) {
    phase_ = static_cast<uint32_t>(static_cast<double>(phase01) * 4294967296.0);
  }
  void reset() { phase_ = 0; }
  inline float next() {
    const uint32_t idx = phase_ >> (32 - kTableBits);
    const float frac = static_cast<float>((phase_ >> 8) & 0xFFFu) * (1.0f / 4096.0f);
    phase_ += inc_;
    const float a = table_[idx];
    const float b = table_[idx + 1];
    return a + (b - a) * frac;
  }

 private:
  const float* table_ = nullptr;
  uint32_t phase_ = 0;
  uint32_t inc_ = 0;
};

constexpr int kTableSize = TableOsc::kTableSize;

// Build a periodic wavetable from harmonic amplitude/phase pairs. `size`
// entries plus one guard entry are written.
inline void buildTable(std::vector<float>& out, int harmonics,
                       const float* amps, const float* phases) {
  out.assign(static_cast<size_t>(kTableSize) + 1, 0.0f);
  float peak = 0.0f;
  for (int i = 0; i < kTableSize; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(kTableSize);
    double v = 0.0;
    for (int h = 0; h < harmonics; ++h) {
      v += static_cast<double>(amps[h]) *
           std::sin(2.0 * 3.14159265358979323846 * static_cast<double>(h + 1) * t +
                    static_cast<double>(phases[h]));
    }
    out[static_cast<size_t>(i)] = static_cast<float>(v);
    peak = std::max(peak, std::fabs(static_cast<float>(v)));
  }
  if (peak > 1e-6f) {
    const float g = 1.0f / peak;
    for (int i = 0; i < kTableSize; ++i) out[static_cast<size_t>(i)] *= g;
  }
  out[static_cast<size_t>(kTableSize)] = out[0];
}

// Soft-clipping helper used by the rotary amplifier stage.
inline float softClip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

}  // namespace dsp
}  // namespace b3
