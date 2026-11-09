// Hammond scanner vibrato / chorus.
//
// The real scanner is a rotating capacitor with eight poles sweeping a tapped
// LC delay line at about 6 Hz. Sending the whole busbar through it produces
// both pitch modulation (Doppler in the line) and a little amplitude ripple
// from the 8 switching events per revolution. Chorus settings mix the dry
// busbar back in. Percussion deliberately bypasses this block.
#pragma once

#include "dsp_common.h"

namespace b3 {

class Scanner {
 public:
  void prepare(double sampleRate);
  void reset();
  void setMode(int mode, float rateHz);
  void process(float* buf, int frames);
  float angle() const { return static_cast<float>(phase_); }

 private:
  double sr_ = 48000.0;
  dsp::DelayLine line_;
  dsp::Biquad lineLpf_;
  dsp::Smoothed depth_;
  dsp::Smoothed mix_;
  dsp::Smoothed am_;
  double phase_ = 0.0;
  float rate_ = 6.0f;
  int mode_ = 0;
};

}  // namespace b3
