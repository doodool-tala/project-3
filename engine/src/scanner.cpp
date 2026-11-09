#include "scanner.h"

#include <cmath>

namespace b3 {

void Scanner::prepare(double sampleRate) {
  sr_ = sampleRate;
  line_.resize(static_cast<int>(0.05 * sr_));
  line_.clear();
  lineLpf_.lowpass(sr_, 9500.0, 0.6);
  depth_.setTimeMs(sr_, 35.0);
  mix_.setTimeMs(sr_, 35.0);
  am_.setTimeMs(sr_, 35.0);
  depth_.snap(0.0f);
  mix_.snap(0.0f);
  am_.snap(0.0f);
  phase_ = 0.0;
  mode_ = 0;
}

void Scanner::reset() {
  line_.clear();
  lineLpf_.reset();
  phase_ = 0.0;
}

void Scanner::setMode(int mode, float rateHz) {
  mode_ = mode;
  rate_ = dsp::clampf(rateHz, 0.5f, 12.0f);

  // Depth, wet mix and switching ripple for OFF, V1..V3, C1..C3. Vibrato is
  // fully wet; chorus blends the unmodulated busbar back in.
  static const float kDepth[7] = {0.0f, 0.30f, 0.55f, 0.85f, 0.35f, 0.60f, 0.90f};
  static const float kMix[7] = {0.0f, 1.0f, 1.0f, 1.0f, 0.55f, 0.60f, 0.62f};
  static const float kAm[7] = {0.0f, 0.10f, 0.13f, 0.16f, 0.06f, 0.08f, 0.10f};

  const int m = (mode_ >= 0 && mode_ <= 6) ? mode_ : 0;
  depth_.target = kDepth[m];
  mix_.target = kMix[m];
  am_.target = kAm[m];
}

void Scanner::process(float* buf, int frames) {
  const double w = 6.283185307179586 * static_cast<double>(rate_) / sr_;
  for (int i = 0; i < frames; ++i) {
    const float dry = buf[i];
    line_.write(dry);

    const float d = depth_.next();
    const float m = mix_.next();
    const float am = am_.next();

    const double ph = phase_ * 6.283185307179586;
    const float lfo = static_cast<float>(std::sin(ph));
    // 0.9 ms nominal line delay, modulated by up to ~0.65 ms.
    const float delaySamples = static_cast<float>(sr_) * 0.00090f +
                               static_cast<float>(sr_) * 0.00065f * d *
                                   (0.5f + 0.5f * lfo);
    float wet = line_.readCubic(delaySamples);
    wet = lineLpf_.process(wet);
    // 8-pole switching ripple plus a touch of amplitude modulation.
    wet *= 1.0f - am * (0.5f + 0.5f * static_cast<float>(std::sin(ph + 0.9f)));
    buf[i] = dry + (wet - dry) * m;

    phase_ += w;
    if (phase_ >= 1.0) phase_ -= 1.0;
  }
}

}  // namespace b3
