// 91-wheel tonewheel generator bank.
//
// Every wheel is a free-running table oscillator. Wheels never stop: keys and
// drawbars only decide how much of each wheel reaches the busbar, which is why
// re-triggering a note on a real B-3 does not restart a phase. The bank also
// carries pickup leakage, magnetic crosstalk between adjacent wheels and the
// mains hum induced in the matching transformer.
#pragma once

#include <cstdint>
#include <vector>

#include "b3/model.h"
#include "dsp_common.h"

namespace b3 {

class TonewheelBank {
 public:
  void prepare(double sampleRate);
  void reset();

  // Mechanical condition of the generator (0 = freshly calibrated,
  // 1 = well-used): wheel trim spreads, slow eccentricity drift.
  void setWear(float wear01);
  void setLeakage(float amount01);
  void setHum(float amount01);

  double wheelFrequency(int wheel) const;

  // Small fixed pickup bleed weight per wheel (0..1), the basis of the
  // Hammond "leakage" bed.
  float leakageWeight(int wheel) const { return leak_[wheel]; }

  // Render the summed generator bus. Wheel gains are linearly interpolated
  // across the block to keep busbar changes click-free.
  void process(const float* gainStart, const float* gainEnd, int frames,
               float* monoOut);

 private:
  void refreshIncrements();
  void buildTables();

  double sr_ = 48000.0;
  uint32_t phase_[kNumWheels] = {};
  uint32_t inc_[kNumWheels] = {};
  float freq_[kNumWheels] = {};
  float trim_[kNumWheels] = {};
  int rich_[kNumWheels] = {};
  float leak_[kNumWheels] = {};
  float driftPhase_[kNumWheels] = {};
  float driftInc_[kNumWheels] = {};
  float driftAmt_[kNumWheels] = {};
  float prev_[kNumWheels] = {};

  std::vector<float> tables_[3];
  float wear_ = 0.35f;
  float leakage_ = 0.3f;
  float hum_ = 0.25f;
  double humPhase_[4] = {};
  double humInc_[4] = {};
  int refreshCounter_ = 0;

  dsp::Rng rng_{0x51ED2701u};
};

}  // namespace b3
