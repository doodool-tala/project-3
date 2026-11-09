#include "percussion.h"

#include <cmath>
#include <vector>

namespace b3 {
namespace {
const float kPercTable[4] = {1.0f, 0.03f, 0.008f, 0.0f};
const float kPercPhase[4] = {0.0f, 0.4f, 1.3f, 0.0f};
}  // namespace

void Percussion::prepare(double sampleRate) {
  sr_ = sampleRate;
  static std::vector<float> table;
  if (table.empty()) dsp::buildTable(table, 3, kPercTable, kPercPhase);
  osc_.setTable(table.data());
  osc_.reset();
  clickBand_.bandpass(sr_, 2600.0, 0.9);
  env_ = 0.0f;
  clickEnv_ = 0.0f;
}

void Percussion::reset() {
  env_ = 0.0f;
  clickEnv_ = 0.0f;
  attacking_ = false;
  osc_.reset();
  clickBand_.reset();
}

void Percussion::trigger(int wheel, bool useSecond, bool slow, float level) {
  wheel_ = wheel;
  // The percussion contact taps a wheel directly; using the wheel frequency
  // (not equal temperament) keeps its beating with the drawbar partials.
  const double hz = wheelFrequency(wheel) * (useSecond ? 1.0 : 1.0);
  osc_.setFrequency(sr_, hz);
  osc_.setPhase(0.0f);
  attackInc_ = static_cast<float>(1.0 / (0.0018 * sr_));
  const double tau = slow ? 0.34 : 0.115;
  decay_ = static_cast<float>(std::exp(-1.0 / (tau * sr_)));
  release_ = static_cast<float>(std::exp(-1.0 / (0.004 * sr_)));
  level_ = dsp::clampf(level, 0.0f, 1.5f);
  env_ = 0.0f;
  attacking_ = true;
  clickEnv_ = 1.0f;
  clickDecay_ = static_cast<float>(std::exp(-1.0 / (0.006 * sr_)));
}

void Percussion::release() {
  // Percussion has its own key contact and is cut when the last key is let go.
  env_ *= 0.0f;
  clickEnv_ = 0.0f;
}

void Percussion::process(float* buf, int frames) {
  if (env_ < 1e-5f && clickEnv_ < 1e-5f) return;
  for (int i = 0; i < frames; ++i) {
    if (attacking_) {
      env_ += attackInc_;
      if (env_ >= 1.0f) {
        env_ = 1.0f;
        attacking_ = false;
      }
    } else {
      env_ *= decay_;
    }
    if (env_ < 1e-6f) env_ = 0.0f;

    float s = osc_.next() * env_ * level_;
    if (clickEnv_ > 1e-5f) {
      clickEnv_ *= clickDecay_;
      s += clickBand_.process(rng_.bipolar()) * clickEnv_ * 0.22f;
    }
    buf[i] += s * 0.55f;
  }
}

}  // namespace b3
