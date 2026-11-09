// B-3 percussion: a single-triggered 2nd or 3rd harmonic with a fast or slow
// decay, taken from the tonewheel bank but routed around the scanner.
#pragma once

#include "b3/model.h"
#include "dsp_common.h"

namespace b3 {

class Percussion {
 public:
  void prepare(double sampleRate);
  void reset();
  bool active() const { return env_ > 1e-4f; }
  void trigger(int wheel, bool useSecond, bool slow, float level);
  void release();
  void process(float* buf, int frames);

 private:
  double sr_ = 48000.0;
  dsp::TableOsc osc_;
  dsp::Biquad clickBand_;
  dsp::Rng rng_{0x7F4A7C15u};
  float env_ = 0.0f;
  float decay_ = 0.0f;
  float attackInc_ = 0.0f;
  float release_ = 0.0f;
  float level_ = 1.0f;
  float clickEnv_ = 0.0f;
  float clickDecay_ = 0.0f;
  bool attacking_ = false;
  int wheel_ = 0;
};

}  // namespace b3
