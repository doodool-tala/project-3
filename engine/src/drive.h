// Organ preamp and matching transformer, plus the B-3 tone control network.
// The tube stage is anti-aliased with first-order ADAA around tanh, which
// keeps the asymmetric saturation from folding back at high drive.
#pragma once

#include "dsp_common.h"

namespace b3 {

class Preamp {
 public:
  void prepare(double sampleRate);
  void reset();
  void setDrive(float drive01);
  void setTone(float tone01);
  void process(float* buf, int frames);
  float driveGain() const { return driveGain_; }

 private:
  static float antiderivative(float x);
  float saturate(float x);

  double sr_ = 48000.0;
  dsp::Biquad inputHp_;
  dsp::Biquad dcBlock_;
  dsp::Biquad lowShelf_;
  dsp::Biquad midDip_;
  dsp::Biquad highShelf_;
  float x1_ = 0.0f;
  float f1_ = 0.0f;
  float driveGain_ = 1.0f;
  float makeup_ = 1.0f;
  float bias_ = 0.0f;
  float offset_ = 0.0f;
  float drive_ = 0.25f;
  float tone_ = 0.5f;
  bool coeffsDirty_ = true;
};

}  // namespace b3
