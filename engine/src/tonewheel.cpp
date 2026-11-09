#include "tonewheel.h"

#include <cmath>

namespace b3 {
namespace {
// Pickup non-linearity: the wheel tooth induces a slightly asymmetric pulse in
// the pickup, so a "sine" wheel carries small even/odd harmonics. Three tables
// are prepared and selected by wheel register to avoid aliasing at the top.
const float kRichA[5] = {1.0f, 0.055f, 0.028f, 0.010f, 0.004f};
const float kRichAPhase[5] = {0.0f, 0.7f, 1.9f, 2.6f, 3.4f};
const float kRichB[4] = {1.0f, 0.030f, 0.012f, 0.004f};
const float kRichBPhase[4] = {0.0f, 1.1f, 2.4f, 3.0f};
const float kRichC[2] = {1.0f, 0.010f};
const float kRichCPhase[2] = {0.0f, 0.5f};

// Overall busbar scaling: keeps a full 8-drawbar chord in a sensible range
// before the preamp/limiter, while leaving headroom for big registrations.
constexpr float kBusGain = 0.150f;
constexpr float kCrosstalk = 0.022f;

float softSaturate(float x) {
  return x / (1.0f + std::fabs(x) * 0.35f);
}
}  // namespace

void TonewheelBank::buildTables() {
  dsp::buildTable(tables_[0], 5, kRichA, kRichAPhase);
  dsp::buildTable(tables_[1], 4, kRichB, kRichBPhase);
  dsp::buildTable(tables_[2], 2, kRichC, kRichCPhase);
}

void TonewheelBank::prepare(double sampleRate) {
  sr_ = sampleRate;
  buildTables();
  for (int w = 0; w < kNumWheels; ++w) {
    freq_[w] = static_cast<float>(wheelFrequency(w));
    phase_[w] = rng_.next();  // free-running, non-unison start
    trim_[w] = 0.86f + 0.26f * rng_.uni();
    leak_[w] = 0.25f + 0.75f * rng_.uni();
    driftAmt_[w] = 0.0f;
    driftInc_[w] = 0.04f + 0.5f * rng_.uni();
    driftPhase_[w] = rng_.uni() * 6.2831853f;
    rich_[w] = freq_[w] < 700.0f ? 0 : (freq_[w] < 2200.0f ? 1 : 2);
    prev_[w] = 0.0f;
  }
  // Mains-related hum: 60 Hz and its first few harmonics. The generator is
  // synchronous with the mains, so these are always harmonically locked.
  const double humFreqs[4] = {60.0, 120.0, 180.0, 240.0};
  for (int i = 0; i < 4; ++i) {
    humInc_[i] = humFreqs[i] / sr_;
    humPhase_[i] = rng_.uni();
  }
  refreshIncrements();
  refreshCounter_ = 0;
}

void TonewheelBank::reset() {
  for (int w = 0; w < kNumWheels; ++w) {
    phase_[w] = rng_.next();
    prev_[w] = 0.0f;
    driftPhase_[w] = rng_.uni() * 6.2831853f;
  }
  for (int i = 0; i < 4; ++i) humPhase_[i] = rng_.uni();
}

void TonewheelBank::setWear(float wear01) {
  wear_ = dsp::clampf(wear01, 0.0f, 1.0f);
  refreshIncrements();
}

void TonewheelBank::setLeakage(float amount01) {
  leakage_ = dsp::clampf(amount01, 0.0f, 1.0f);
}

void TonewheelBank::setHum(float amount01) {
  hum_ = dsp::clampf(amount01, 0.0f, 1.0f);
}

double TonewheelBank::wheelFrequency(int wheel) const {
  return b3::wheelFrequency(wheel);
}

void TonewheelBank::refreshIncrements() {
  for (int w = 0; w < kNumWheels; ++w) {
    // Wear detunes wheels by up to ~2 cents and makes them drift a hair as
    // the gear train runs, the classic "living" generator.
    driftAmt_[w] = wear_ * 0.0014f * (0.4f + 0.6f * ((w * 37) % 11) / 10.0f);
    const double hz = static_cast<double>(freq_[w]) *
                      (1.0 + driftAmt_[w] *
                                 std::sin(static_cast<double>(driftPhase_[w])));
    const double inc = hz / sr_ * 4294967296.0;
    inc_[w] = static_cast<uint32_t>(static_cast<int64_t>(inc));
  }
}

void TonewheelBank::process(const float* gainStart, const float* gainEnd,
                            int frames, float* monoOut) {
  const float invFrames = frames > 1 ? 1.0f / static_cast<float>(frames - 1) : 0.0f;
  const float humGain = hum_ * 0.0060f;

  for (int i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i) * invFrames;
    if (t > 0.999f) {
      // Keep the last block's increments fresh without recomputing inside the
      // inner loop; drift is very slow (a few tenths of a hertz).
      if (refreshCounter_++ >= 128) {
        for (int w = 0; w < kNumWheels; ++w) {
          driftPhase_[w] += driftInc_[w] / static_cast<float>(sr_) * 2048.0f;
          if (driftPhase_[w] > 6.2831853f) driftPhase_[w] -= 6.2831853f;
        }
        refreshIncrements();
        refreshCounter_ = 0;
      }
    }

    float acc = 0.0f;
    for (int w = 0; w < kNumWheels; ++w) {
      const uint32_t p = phase_[w];
      const uint32_t idx = p >> (32 - dsp::TableOsc::kTableBits);
      const float frac = static_cast<float>((p >> 8) & 0xFFFu) * (1.0f / 4096.0f);
      const float* tab = tables_[rich_[w]].data();
      const float a = tab[idx];
      const float s = (a + (tab[idx + 1] - a) * frac) * trim_[w];

      const float g = gainStart[w] + (gainEnd[w] - gainStart[w]) * t;
      acc += g * (s + kCrosstalk * (prev_[w] + prev_[(w + 1) % kNumWheels] +
                                    prev_[(w + 90) % kNumWheels]));
      prev_[w] = s;
      phase_[w] = p + inc_[w];
    }

    if (humGain > 0.0f) {
      float h = 0.0f;
      const float levels[4] = {0.50f, 0.28f, 0.14f, 0.08f};
      for (int k = 0; k < 4; ++k) {
        h += levels[k] * static_cast<float>(std::sin(humPhase_[k] * 6.2831853));
        humPhase_[k] += humInc_[k];
        if (humPhase_[k] >= 1.0) humPhase_[k] -= 1.0;
      }
      acc += h * humGain;
    }

    monoOut[i] = softSaturate(acc * kBusGain);
  }
}

}  // namespace b3
