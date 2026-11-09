// Small feedback-delay-network room for the "space" around the cabinet.
// It is deliberately after the Leslie so the rotor stays dry and present.
#pragma once

#include "dsp_common.h"

namespace b3 {

class Reverb {
 public:
  void prepare(double sampleRate);
  void reset();
  void setParams(float mix01, float size01);
  void process(float* left, float* right, int frames);

 private:
  static constexpr int kLines = 8;
  double sr_ = 48000.0;
  dsp::DelayLine lines_[kLines];
  dsp::DelayLine predelay_;
  int len_[kLines] = {};
  float dampState_[kLines] = {};
  float feedback_[kLines] = {};
  float damp_ = 0.35f;
  float size_ = 0.55f;
  dsp::Smoothed mix_;
};

}  // namespace b3
