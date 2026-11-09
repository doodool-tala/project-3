#include "reverb.h"

#include <cmath>

namespace b3 {
namespace {
// Prime-ish delay lengths at 48 kHz; scaled by sample rate in prepare().
const int kBase[8] = {1471, 1783, 2053, 2381, 2683, 3011, 3347, 3671};

void hadamard(float* d) {
  for (int step = 1; step < 8; step <<= 1) {
    for (int i = 0; i < 8; i += step << 1) {
      for (int j = 0; j < step; ++j) {
        const float a = d[i + j];
        const float b = d[i + j + step];
        d[i + j] = a + b;
        d[i + j + step] = a - b;
      }
    }
  }
  constexpr float kNorm = 0.35355339059f;  // 1/sqrt(8)
  for (int i = 0; i < 8; ++i) d[i] *= kNorm;
}
}  // namespace

void Reverb::prepare(double sampleRate) {
  sr_ = sampleRate;
  for (int i = 0; i < kLines; ++i) {
    len_[i] = static_cast<int>(kBase[i] * sr_ / 48000.0);
    lines_[i].resize(len_[i] + 16);
    lines_[i].clear();
    dampState_[i] = 0.0f;
    feedback_[i] = 0.8f;
  }
  predelay_.resize(static_cast<int>(0.07 * sr_));
  predelay_.clear();
  mix_.setTimeMs(sr_, 40.0);
  mix_.snap(0.0f);
  setParams(0.22f, 0.55f);
}

void Reverb::reset() {
  for (int i = 0; i < kLines; ++i) {
    lines_[i].clear();
    dampState_[i] = 0.0f;
  }
  predelay_.clear();
}

void Reverb::setParams(float mix01, float size01) {
  mix_.target = dsp::clampf(mix01, 0.0f, 1.0f) * 0.75f;
  size_ = dsp::clampf(size01, 0.0f, 1.0f);
  const double rt60 = 0.55 + 4.8 * static_cast<double>(size_);
  for (int i = 0; i < kLines; ++i) {
    const double g = std::pow(10.0, -3.0 * static_cast<double>(len_[i]) /
                                       (rt60 * sr_));
    feedback_[i] = static_cast<float>(std::min(g, 0.985));
  }
  damp_ = 0.22f + 0.25f * size_;
}

void Reverb::process(float* left, float* right, int frames) {
  const float predelaySamples = static_cast<float>(0.018 * sr_);
  for (int n = 0; n < frames; ++n) {
    const float mix = mix_.next();
    const float x = 0.5f * (left[n] + right[n]);
    predelay_.write(x);
    const float in = predelay_.readLinear(predelaySamples);

    float y[kLines];
    float z[kLines];
    for (int i = 0; i < kLines; ++i) {
      float v = lines_[i].readLinear(static_cast<float>(len_[i]));
      dampState_[i] += damp_ * (v - dampState_[i]);
      y[i] = dampState_[i];
      z[i] = y[i];
    }
    hadamard(z);
    for (int i = 0; i < kLines; ++i) {
      lines_[i].write(in * 0.32f + z[i] * feedback_[i]);
    }

    const float wl = (y[0] - y[2] + y[4] - y[6]) * 0.30f;
    const float wr = (y[1] - y[3] + y[5] - y[7]) * 0.30f;
    left[n] = left[n] * (1.0f - mix) + wl * mix;
    right[n] = right[n] * (1.0f - mix) + wr * mix;
  }
}

}  // namespace b3
