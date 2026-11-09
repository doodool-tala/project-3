#include "drive.h"

#include <cmath>

namespace b3 {

void Preamp::prepare(double sampleRate) {
  sr_ = sampleRate;
  inputHp_.highpass(sr_, 28.0, 0.7);
  dcBlock_.highpass(sr_, 16.0, 0.7);
  midDip_.peak(sr_, 750.0, 0.9, -2.5);
  lowShelf_.lowShelf(sr_, 210.0, 0.0);
  highShelf_.highShelf(sr_, 3400.0, 0.0);
  reset();
  setDrive(drive_);
  setTone(tone_);
}

void Preamp::reset() {
  inputHp_.reset();
  dcBlock_.reset();
  lowShelf_.reset();
  midDip_.reset();
  highShelf_.reset();
  x1_ = 0.0f;
  f1_ = 0.0f;
}

void Preamp::setDrive(float drive01) {
  drive_ = dsp::clampf(drive01, 0.0f, 1.0f);
  // 12AU7 input stage: clean at low settings, thick and compressed near max.
  const float d = drive_;
  driveGain_ = 1.0f + 17.0f * d * d + 2.5f * d;
  bias_ = 0.06f + 0.22f * d;
  makeup_ = 1.0f / (0.55f + 0.75f * driveGain_ * 0.30f);
  // Sit the ADAA state at the tube's quiescent operating point and remove the
  // resulting DC, otherwise switching the amp on produces a low thump.
  x1_ = driveGain_ * bias_;
  f1_ = antiderivative(x1_);
  offset_ = std::tanh(driveGain_ * bias_);
  coeffsDirty_ = true;
}

void Preamp::setTone(float tone01) {
  tone_ = dsp::clampf(tone01, 0.0f, 1.0f);
  coeffsDirty_ = true;
}

void Preamp::process(float* buf, int frames) {
  if (coeffsDirty_) {
    const float t = (tone_ - 0.5f) * 2.0f;  // -1..1
    lowShelf_.lowShelf(sr_, 210.0, static_cast<double>(t * 7.0f));
    highShelf_.highShelf(sr_, 3400.0, static_cast<double>(t * 7.5f));
    coeffsDirty_ = false;
  }
  const float outGain = makeup_ * 1.35f;
  for (int i = 0; i < frames; ++i) {
    float x = inputHp_.process(buf[i]);
    // Slight transformer core compression before the tube.
    x = dsp::softClip(x * (0.8f + 0.25f * driveGain_)) * 1.05f;
    const float y = saturate(x);
    float z = dcBlock_.process(y);
    z = lowShelf_.process(z);
    z = midDip_.process(z);
    z = highShelf_.process(z);
    buf[i] = z * outGain;
  }
}

float Preamp::antiderivative(float x) {
  // log(cosh(x)), evaluated without overflow.
  const float ax = std::fabs(x);
  return ax + std::log1p(std::exp(-2.0f * ax)) - 0.69314718f;
}

float Preamp::saturate(float x) {
  // First-order antiderivative anti-aliasing of tanh: F(u) = log(cosh(u)).
  const float u = driveGain_ * (x + bias_);
  const float f = antiderivative(u);
  const float du = u - x1_;
  float y;
  if (std::fabs(du) > 1e-5f) {
    y = (f - f1_) / du;
  } else {
    y = std::tanh(0.5f * (u + x1_));
  }
  x1_ = u;
  f1_ = f;
  return y - offset_;
}

}  // namespace b3
