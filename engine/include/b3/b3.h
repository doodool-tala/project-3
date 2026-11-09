// B-3 physical model: public engine API.
//
// The engine is mono-in/stereo-out, entirely additive and band-limited by
// construction, and designed to run in a real-time audio callback. It has no
// Android dependencies, so the host tests exercise the exact code that ships
// in the APK.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "b3/model.h"

namespace b3 {

struct Params {
  // Drawbar registration, 0..8 for each of the nine bars.
  std::array<float, kNumDrawbars> drawbars{{8.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f}};
  bool percussionOn = false;
  bool percussionSoft = false;
  bool percussionSlow = false;
  bool percussionThird = true;  // true = 3rd harmonic, false = 2nd
  int scanner = 0;              // 0 off, 1..3 vibrato, 4..6 chorus
  int leslieSpeed = 1;          // 0 brake, 1 slow, 2 fast

  float drive = 0.28f;
  float keyClick = 0.55f;
  float leakage = 0.30f;
  float hum = 0.20f;
  float reverb = 0.22f;
  float volume = 0.72f;
  float expression = 1.0f;
  float micDistance = 0.45f;
  float micSpread = 0.50f;
  float hornDrum = 0.50f;
  float tone = 0.50f;
  float scannerRate = 6.0f;
  float wear = 0.35f;
  float cabinet = 1.0f;
};

class Organ {
 public:
  Organ();
  ~Organ();

  void prepare(double sampleRate, int maxFrames);
  void reset();

  // Thread-safe: may be called from the UI or MIDI thread.
  void noteOn(int midiNote, float velocity);
  void noteOff(int midiNote);
  void allNotesOff();
  void setParams(const Params& params);

  void process(float* left, float* right, int frames);
  void processInterleaved(float* interleaved, int frames);

  double sampleRate() const;
  int activeNotes() const;

  // UI telemetry, safe to poll from any thread.
  float hornAngleDegrees() const;
  float drumAngleDegrees() const;
  float scannerAngleDegrees() const;
  float outputPeak() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace b3
