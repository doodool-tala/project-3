// Leslie rotary cabinet: rotating horn and bass drum, two microphones,
// Doppler from the physical path length, directivity amplitude modulation,
// off-axis high-frequency absorption, cabinet colouration and rotor inertia.
#pragma once

#include "dsp_common.h"

namespace b3 {

class Leslie {
 public:
  void prepare(double sampleRate);
  void reset();
  void setParams(float micDistance01, float micSpread01, float hornDrum01,
                 float cabinet01, int speed);
  void process(const float* monoIn, float* left, float* right, int frames);

  float hornAngle() const { return static_cast<float>(hornAngle_); }
  float drumAngle() const { return static_cast<float>(drumAngle_); }
  float speedHz() const { return speed_; }

 private:
  static constexpr int kLut = 512;
  struct Lp1 {
    float z = 0.0f;
    float a = 1.0f;
    inline float process(float x) {
      z += a * (x - z);
      return z;
    }
  };

  void rebuildLuts();
  void updateCoeffs();

  double sr_ = 48000.0;

  dsp::Biquad hornHp1_, hornHp2_, drumLp1_, drumLp2_;
  dsp::Biquad cabHp_, cabPeak_, cabLp_;
  dsp::DelayLine hornLine_, drumLine_;
  dsp::Rng rng_{0x1B873593u};

  float hornDelay_[2][kLut] = {};
  float hornGain_[2][kLut] = {};
  float drumDelay_[2][kLut] = {};
  float drumGain_[2][kLut] = {};
  float hornCut_[2][kLut] = {};
  float drumCut_[2][kLut] = {};

  float micDistance_ = 0.45f;
  float micSpread_ = 0.5f;
  float hornDrum_ = 0.5f;
  float cabinet_ = 1.0f;
  float hornLevel_ = 0.8f;
  float drumLevel_ = 0.8f;
  int speedMode_ = 1;
  float targetSpeed_ = 0.78f;
  float speed_ = 0.0f;
  double hornAngle_ = 0.0;
  double drumAngle_ = 0.0;
  float hornTrim_ = 0.9f;
  float drumTrim_ = 0.9f;
  Lp1 lpL_, lpR_, lpDrumL_, lpDrumR_;
  float noiseZ_ = 0.0f;
  float predelaySamples_ = 0.0f;
};

}  // namespace b3
