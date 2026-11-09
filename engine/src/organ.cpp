#include "b3/b3.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dsp_common.h"
#include "drive.h"
#include "leslie.h"
#include "percussion.h"
#include "reverb.h"
#include "scanner.h"
#include "b3/wiring.h"
#include "tonewheel.h"

namespace b3 {
namespace {

constexpr int kChunk = 16;
constexpr int kMaxClickVoices = 8;
constexpr int kMaxEvents = 128;

inline float barLevel(float value) {
  const float x = dsp::clampf(value, 0.0f, 8.0f) * 0.125f;
  // Smoothstep-ish law; drawbar response is far from linear in level.
  return (x * x * (3.0f - 2.0f * x)) * 1.30f;
}

struct KeyClickVoice {
  bool active = false;
  float env = 0.0f;
  float env2 = 0.0f;
  float decay = 0.0f;
  float decay2 = 0.0f;
  dsp::Biquad band;
  dsp::Rng rng{0x9E3779B9u};

  void trigger(double sr, float noteHz, float amount) {
    active = true;
    env = amount * 0.60f;
    env2 = amount * 0.22f;
    const double center = static_cast<double>(
        dsp::clampf(noteHz * 3.4f, 900.0f, 5200.0f));
    band.bandpass(sr, center, 0.85);
    decay = static_cast<float>(std::exp(-1.0 / (0.0022 * sr)));
    decay2 = static_cast<float>(std::exp(-1.0 / (0.0080 * sr)));
  }

  inline float process() {
    if (!active) return 0.0f;
    env *= decay;
    env2 *= decay2;
    const float n = band.process(rng.bipolar());
    const float out = n * (env + env2 * 0.45f);
    if (env < 1e-5f && env2 < 1e-5f) active = false;
    return out;
  }
};

struct OutputStage {
  float env = 0.0f;
  float gain = 1.0f;
  float attack = 0.002f;
  float release = 0.0001f;
  float dcL = 0.0f, dcR = 0.0f;
  float dcCoef = 0.001f;

  void prepare(double sr) {
    attack = static_cast<float>(1.0 - std::exp(-1.0 / (0.0012 * sr)));
    release = static_cast<float>(1.0 - std::exp(-1.0 / (0.150 * sr)));
    dcCoef = static_cast<float>(1.0 - std::exp(-2.0 * 3.14159265 * 9.0 / sr));
  }

  inline void process(float& l, float& r) {
    dcL += dcCoef * (l - dcL);
    dcR += dcCoef * (r - dcR);
    l -= dcL;
    r -= dcR;

    const float peak = std::max(std::fabs(l), std::fabs(r));
    if (peak > env) {
      env += (peak - env) * attack;
    } else {
      env += (peak - env) * release;
    }
    const float target = env > 0.88f ? 0.88f / std::max(env, 1e-6f) : 1.0f;
    if (target < gain) {
      gain += (target - gain) * 0.30f;
    } else {
      gain += (target - gain) * 0.0020f;
    }
    l = dsp::softClip(l * gain);
    r = dsp::softClip(r * gain);
    if (!std::isfinite(l)) l = 0.0f;
    if (!std::isfinite(r)) r = 0.0f;
  }
};

}  // namespace

struct Organ::Impl {
  struct Event {
    int note = 0;
    bool on = false;
    float velocity = 1.0f;
    bool allOff = false;
  };

  struct Route {
    int wheel[kNumDrawbars] = {};
    float amp[kNumDrawbars] = {};
  };

  struct Key {
    bool held = false;
    float gate = 0.0f;
  };

  double sr = 48000.0;
  int maxFrames = 512;

  Params params;
  Params staged;
  std::mutex stateMutex;
  std::vector<Event> pending;
  std::vector<Event> eventScratch;
  bool paramsDirty = false;

  TonewheelBank wheels;
  Scanner scanner;
  Percussion percussion;
  Preamp preamp;
  Leslie leslie;
  Reverb reverb;
  OutputStage output;

  Route routes[kNumKeys];
  Key keys[kNumKeys];

  float barLv[kNumDrawbars] = {};
  float gainTarget[kNumWheels] = {};
  float gainCurrent[kNumWheels] = {};
  float gainNext[kNumWheels] = {};

  KeyClickVoice clicks[kMaxClickVoices];
  int clickCursor = 0;

  std::vector<float> bus;
  std::vector<float> postScanner;
  std::vector<float> mono;
  std::vector<float> outL;
  std::vector<float> outR;

  dsp::Smoothed expression;
  dsp::Smoothed master;

  std::atomic<float> hornDeg{0.0f};
  std::atomic<float> drumDeg{0.0f};
  std::atomic<float> scannerDeg{0.0f};
  std::atomic<float> peakOut{0.0f};

  void prepare(double sampleRate, int frames) {
    sr = sampleRate;
    maxFrames = std::max(frames, 64);
    bus.assign(maxFrames, 0.0f);
    postScanner.assign(maxFrames, 0.0f);
    mono.assign(maxFrames, 0.0f);
    outL.assign(maxFrames, 0.0f);
    outR.assign(maxFrames, 0.0f);

    wheels.prepare(sr);
    scanner.prepare(sr);
    percussion.prepare(sr);
    preamp.prepare(sr);
    leslie.prepare(sr);
    reverb.prepare(sr);
    output.prepare(sr);

    expression.setTimeMs(sr, 22.0);
    master.setTimeMs(sr, 30.0);
    expression.snap(params.expression);
    master.snap(params.volume);

    for (int k = 0; k < kNumKeys; ++k) {
      keys[k] = Key{};
      for (int d = 0; d < kNumDrawbars; ++d) {
        routes[k].wheel[d] = 0;
        routes[k].amp[d] = 1.0f;
      }
    }
    updateRoutes();
    applyParams(params);
    for (int w = 0; w < kNumWheels; ++w) {
      gainCurrent[w] = gainTarget[w];
      gainNext[w] = gainTarget[w];
    }
  }

  void updateRoutes() {
    for (int k = 0; k < kNumKeys; ++k) {
      const double hz = midiToHz(kLowestNote + k);
      for (int d = 0; d < kNumDrawbars; ++d) {
        const int wired = wiredWheel(k, d);
        if (wired >= 0) {
          routes[k].wheel[d] = wired;
        } else {
          const PartialMap m = mapPartial(hz, drawbarHarmonic(d));
          routes[k].wheel[d] = m.wheel;
        }
        routes[k].amp[d] = drawbarTaper(d);
      }
    }
  }

  void applyParams(const Params& p) {
    wheels.setWear(p.wear);
    wheels.setLeakage(p.leakage);
    wheels.setHum(p.hum);
    scanner.setMode(p.scanner, p.scannerRate);
    preamp.setDrive(p.drive);
    preamp.setTone(p.tone);
    leslie.setParams(p.micDistance, p.micSpread, p.hornDrum, p.cabinet,
                     p.leslieSpeed);
    reverb.setParams(p.reverb, 0.62f);
    for (int d = 0; d < kNumDrawbars; ++d) barLv[d] = barLevel(p.drawbars[d]);
  }

  void reset() {
    wheels.reset();
    scanner.reset();
    percussion.reset();
    preamp.reset();
    leslie.reset();
    reverb.reset();
    for (int k = 0; k < kNumKeys; ++k) keys[k] = Key{};
    for (int w = 0; w < kNumWheels; ++w) {
      gainTarget[w] = 0.0f;
      gainCurrent[w] = 0.0f;
      gainNext[w] = 0.0f;
    }
    for (auto& c : clicks) c.active = false;
  }

  void drain() {
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      if (paramsDirty) {
        params = staged;
        paramsDirty = false;
        applyParams(params);
      }
      if (!pending.empty()) eventScratch.swap(pending);
    }
    for (const Event& e : eventScratch) {
      if (e.allOff) {
        for (int k = 0; k < kNumKeys; ++k) keys[k].held = false;
        percussion.release();
      } else if (e.on) {
        noteOnInternal(e.note, e.velocity);
      } else {
        noteOffInternal(e.note);
      }
    }
    eventScratch.clear();
  }

  void triggerClick(float noteHz, float amount) {
    KeyClickVoice& v = clicks[clickCursor];
    clickCursor = (clickCursor + 1) % kMaxClickVoices;
    v.trigger(sr, noteHz, amount);
  }

  void noteOnInternal(int midi, float velocity) {
    if (midi < kLowestNote || midi > kHighestNote) return;
    const int k = midi - kLowestNote;
    keys[k].held = true;
    const float vel = dsp::clampf(velocity, 0.2f, 1.0f);
    const float noteHz = static_cast<float>(midiToHz(midi));

    if (params.keyClick > 0.001f) {
      triggerClick(noteHz, params.keyClick * (0.75f + 0.25f * vel));
    }

    if (params.percussionOn) {
      // Percussion taps the same busbar contact as the 2 2/3' (3rd harmonic)
      // or 4' (2nd harmonic) drawbar, following the manual's actual wiring.
      const int bar = params.percussionThird ? 4 : 3;
      const int wheel = routes[k].wheel[bar];
      const float level = params.percussionSoft ? 0.50f : 1.0f;
      percussion.trigger(wheel, !params.percussionThird,
                         params.percussionSlow, level);
    }
  }

  void noteOffInternal(int midi) {
    if (midi < kLowestNote || midi > kHighestNote) return;
    const int k = midi - kLowestNote;
    if (!keys[k].held) return;
    keys[k].held = false;
    if (params.keyClick > 0.001f) {
      triggerClick(static_cast<float>(midiToHz(midi)), params.keyClick * 0.30f);
    }
    bool anyHeld = false;
    for (int i = 0; i < kNumKeys; ++i) {
      if (keys[i].held) {
        anyHeld = true;
        break;
      }
    }
    if (!anyHeld) percussion.release();
  }

  void updateChunk(int frames) {
    const float attackCoef =
        static_cast<float>(1.0 - std::exp(-static_cast<double>(frames) /
                                          (0.0018 * sr)));
    const float releaseCoef =
        static_cast<float>(1.0 - std::exp(-static_cast<double>(frames) /
                                          (0.0050 * sr)));

    for (int w = 0; w < kNumWheels; ++w) {
      // Pickup bleed: audible, but far below the busbar signal.
      gainTarget[w] = params.leakage * wheels.leakageWeight(w) * 0.006f;
    }

    for (int k = 0; k < kNumKeys; ++k) {
      Key& key = keys[k];
      const float target = key.held ? 1.0f : 0.0f;
      key.gate += (target - key.gate) * (key.held ? attackCoef : releaseCoef);
      if (key.gate > 1e-4f) {
        const Route& r = routes[k];
        for (int d = 0; d < kNumDrawbars; ++d) {
          gainTarget[r.wheel[d]] += key.gate * r.amp[d] * barLv[d];
        }
      }
    }

    // Interpolate the busbar gains across this chunk.
    for (int w = 0; w < kNumWheels; ++w) {
      gainNext[w] = gainTarget[w];
    }
  }

  void render(float* left, float* right, int frames) {
    drain();

    int off = 0;
    while (off < frames) {
      const int n = std::min(kChunk, frames - off);
      updateChunk(n);
      wheels.process(gainCurrent, gainNext, n, bus.data());

      // Key clicks belong to the busbar and therefore pass the scanner.
      for (int i = 0; i < n; ++i) {
        float c = 0.0f;
        for (auto& v : clicks) c += v.process();
        bus[static_cast<size_t>(i)] += c * 0.8f;
      }

      std::memcpy(postScanner.data(), bus.data(), sizeof(float) * static_cast<size_t>(n));
      scanner.process(postScanner.data(), n);
      percussion.process(postScanner.data(), n);
      for (int i = 0; i < n; ++i) {
        const float expv = expression.next();
        postScanner[static_cast<size_t>(i)] *= expv;
      }
      preamp.process(postScanner.data(), n);

      leslie.process(postScanner.data(), outL.data(), outR.data(), n);
      reverb.process(outL.data(), outR.data(), n);

      float blockPeak = 0.0f;
      for (int i = 0; i < n; ++i) {
        const float level = master.next();
        float l = outL[static_cast<size_t>(i)] * level;
        float r = outR[static_cast<size_t>(i)] * level;
        output.process(l, r);
        left[off + i] = l;
        right[off + i] = r;
        blockPeak = std::max(blockPeak, std::max(std::fabs(l), std::fabs(r)));
      }
      peakOut.store(blockPeak, std::memory_order_relaxed);
      for (int w = 0; w < kNumWheels; ++w) gainCurrent[w] = gainNext[w];
      off += n;
    }

    hornDeg.store(leslie.hornAngle() * 57.2957795f, std::memory_order_relaxed);
    drumDeg.store(leslie.drumAngle() * 57.2957795f, std::memory_order_relaxed);
    scannerDeg.store(scanner.angle() * 360.0f, std::memory_order_relaxed);
  }
};

Organ::Organ() : impl_(new Impl()) {}
Organ::~Organ() = default;

void Organ::prepare(double sampleRate, int maxFrames) {
  impl_->prepare(sampleRate, maxFrames);
}

void Organ::reset() { impl_->reset(); }

void Organ::noteOn(int midiNote, float velocity) {
  std::lock_guard<std::mutex> lock(impl_->stateMutex);
  if (impl_->pending.size() >= kMaxEvents) return;
  impl_->pending.push_back({midiNote, true, velocity});
}

void Organ::noteOff(int midiNote) {
  std::lock_guard<std::mutex> lock(impl_->stateMutex);
  if (impl_->pending.size() >= kMaxEvents) return;
  impl_->pending.push_back({midiNote, false, 0.0f});
}

void Organ::allNotesOff() {
  std::lock_guard<std::mutex> lock(impl_->stateMutex);
  impl_->pending.clear();
  impl_->pending.push_back({0, false, 0.0f, true});
}

void Organ::setParams(const Params& params) {
  std::lock_guard<std::mutex> lock(impl_->stateMutex);
  impl_->staged = params;
  impl_->paramsDirty = true;
}

void Organ::process(float* left, float* right, int frames) {
  int off = 0;
  while (off < frames) {
    const int n = std::min(frames - off, impl_->maxFrames);
    impl_->render(left + off, right + off, n);
    off += n;
  }
}

void Organ::processInterleaved(float* interleaved, int frames) {
  if (frames <= 0) return;
  // Render in slices so the scratch buffers stay bounded.
  int off = 0;
  while (off < frames) {
    const int n = std::min(frames - off, impl_->maxFrames);
    impl_->render(impl_->outL.data(), impl_->outR.data(), n);
    for (int i = 0; i < n; ++i) {
      interleaved[2 * (off + i)] = impl_->outL[static_cast<size_t>(i)];
      interleaved[2 * (off + i) + 1] = impl_->outR[static_cast<size_t>(i)];
    }
    off += n;
  }
}

double Organ::sampleRate() const { return impl_->sr; }

int Organ::activeNotes() const {
  std::lock_guard<std::mutex> lock(impl_->stateMutex);
  int count = 0;
  for (const auto& key : impl_->keys) {
    if (key.held) ++count;
  }
  return count;
}

float Organ::hornAngleDegrees() const {
  return impl_->hornDeg.load(std::memory_order_relaxed);
}
float Organ::drumAngleDegrees() const {
  return impl_->drumDeg.load(std::memory_order_relaxed);
}
float Organ::scannerAngleDegrees() const {
  return impl_->scannerDeg.load(std::memory_order_relaxed);
}
float Organ::outputPeak() const {
  return impl_->peakOut.load(std::memory_order_relaxed);
}

}  // namespace b3
