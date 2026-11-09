#include "b3/model.h"

#include <cmath>

namespace b3 {
namespace {
const double kSemitone = 1.0 / 12.0;
const int kFoldAt = 91;         // number of wired generator wheels
const int kOctaveSemitones = 12;

// Classic drawbar labels, low to high.
const char* const kLabels[kNumDrawbars] = {
    "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'",
};

// Relative pickup/busbar taper per drawbar. The 16' and the top octave
// bars sit lower on the matching transformer than the fundamental and
// twelfth, just like the real busbar resistance network.
const float kTaper[kNumDrawbars] = {
    0.86f, 0.94f, 1.00f, 0.97f, 0.92f, 0.89f, 0.85f, 0.82f, 0.79f,
};

const double kHarmonic[kNumDrawbars] = {
    0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0,
};
}  // namespace

double midiToHz(double midi) {
  return 440.0 * std::pow(2.0, (midi - 69.0) * kSemitone);
}

double wheelFrequency(int wheel) {
  return kWheelBaseHz * std::pow(2.0, static_cast<double>(wheel) * kSemitone);
}

double drawbarHarmonic(int index) {
  if (index < 0) index = 0;
  if (index >= kNumDrawbars) index = kNumDrawbars - 1;
  return kHarmonic[index];
}

float drawbarTaper(int index) {
  if (index < 0) index = 0;
  if (index >= kNumDrawbars) index = kNumDrawbars - 1;
  return kTaper[index];
}

const char* drawbarLabel(int index) {
  if (index < 0) index = 0;
  if (index >= kNumDrawbars) index = kNumDrawbars - 1;
  return kLabels[index];
}

PartialMap mapPartial(double noteHz, double harmonicRatio) {
  PartialMap out{0, 0};
  const double target = noteHz * harmonicRatio;
  if (target <= 0.0) return out;

  // Nearest generator wheel to the desired partial.
  const double exact = std::log2(target / kWheelBaseHz) * 12.0;
  int wheel = static_cast<int>(std::lround(exact));
  int folded = 0;

  // Foldback: the physical generator only has 91 wheels. The B-3 harness
  // re-uses wheels an octave away at the top and bottom of the compass.
  while (wheel < 0) {
    wheel += kOctaveSemitones;
    ++folded;
  }
  while (wheel >= kFoldAt) {
    wheel -= kOctaveSemitones;
    --folded;
  }

  out.wheel = wheel;
  out.octavesFolded = folded;
  return out;
}

}  // namespace b3
