// Host verification for the portable B-3 engine.
//
// These tests exercise the shipped C++ (the same sources compiled into the
// APK): tuning/foldback, note generation, percussion decay, scanner activity,
// rotor inertia, limiter stability, release behaviour and CPU cost.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "b3/b3.h"
#include "b3/model.h"
#include "b3/wiring.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double goertzel(const std::vector<float>& x, size_t begin, size_t len,
                double freq) {
  if (begin + len > x.size() || len == 0) return 0.0;
  const double w = 2.0 * kPi * freq / kSr;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = 0; i < len; ++i) {
    const double s0 = static_cast<double>(x[begin + i]) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
  return std::sqrt(std::max(0.0, power)) / static_cast<double>(len);
}

double rms(const std::vector<float>& x, size_t begin, size_t len) {
  if (begin + len > x.size() || len == 0) return 0.0;
  double sum = 0.0;
  for (size_t i = 0; i < len; ++i) {
    const double v = x[begin + i];
    sum += v * v;
  }
  return std::sqrt(sum / static_cast<double>(len));
}

b3::Params cleanParams() {
  b3::Params p;
  p.drawbars = {{0, 0, 8, 0, 0, 0, 0, 0, 0}};
  p.percussionOn = false;
  p.scanner = 0;
  p.leslieSpeed = 0;
  p.drive = 0.0f;
  p.keyClick = 0.0f;
  p.leakage = 0.0f;
  p.hum = 0.0f;
  p.reverb = 0.0f;
  p.volume = 0.9f;
  p.expression = 1.0f;
  p.wear = 0.0f;
  p.tone = 0.5f;
  return p;
}

struct RenderResult {
  std::vector<float> left;
  std::vector<float> right;
  float peak = 0.0f;
  bool finite = true;
};

// Render `seconds` into fresh buffers, with an optional parameter update
// before starting.
RenderResult render(b3::Organ& organ, double seconds,
                    const b3::Params* params = nullptr) {
  RenderResult r;
  if (params) organ.setParams(*params);
  const size_t frames = static_cast<size_t>(seconds * kSr);
  r.left.resize(frames);
  r.right.resize(frames);
  const int block = 192;
  std::vector<float> l(block), rr(block);
  size_t done = 0;
  while (done < frames) {
    const int n = static_cast<int>(std::min<size_t>(block, frames - done));
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(rr.begin(), rr.end(), 0.0f);
    organ.process(l.data(), rr.data(), n);
    for (int i = 0; i < n; ++i) {
      const float a = l[static_cast<size_t>(i)];
      const float b = rr[static_cast<size_t>(i)];
      r.left[done + static_cast<size_t>(i)] = a;
      r.right[done + static_cast<size_t>(i)] = b;
      if (!std::isfinite(a) || !std::isfinite(b)) r.finite = false;
      r.peak = std::max(r.peak, std::max(std::fabs(a), std::fabs(b)));
    }
    done += static_cast<size_t>(n);
  }
  return r;
}

void testModel() {
  std::printf("[model] tuning + foldback\n");
  CHECK(std::fabs(b3::wheelFrequency(0) - 32.703195) < 1e-5);
  CHECK(std::fabs(b3::wheelFrequency(12) - 65.406391) < 1e-4);
  CHECK(b3::wheelFrequency(90) > 5700.0 && b3::wheelFrequency(90) < 6000.0);

  CHECK(std::fabs(b3::drawbarHarmonic(0) - 0.5) < 1e-9);
  CHECK(std::fabs(b3::drawbarHarmonic(2) - 1.0) < 1e-9);
  CHECK(std::fabs(b3::drawbarHarmonic(4) - 3.0) < 1e-9);
  CHECK(std::fabs(b3::drawbarHarmonic(8) - 8.0) < 1e-9);

  // 61-key B-3 manual: C2 (65.4 Hz) .. C7 (2093 Hz).
  CHECK(b3::kNumKeys == 61);
  CHECK(std::fabs(b3::midiToHz(b3::kLowestNote) - 65.4064) < 0.01);
  CHECK(std::fabs(b3::midiToHz(b3::kHighestNote) - 2093.005) < 0.1);

  // Generic octave foldback helper.
  const b3::PartialMap low = b3::mapPartial(21.83, 1.0);
  CHECK(low.wheel == 5);
  CHECK(low.octavesFolded == 1);
  const b3::PartialMap high = b3::mapPartial(b3::midiToHz(96), 8.0);
  CHECK(high.wheel == 84);
  CHECK(high.octavesFolded == -2);

  // The service-manual harness, including its asymmetric foldback.
  CHECK(b3::wiredWheel(0, 0) == 12);   // C2 16' folds up to unison
  CHECK(b3::wiredWheel(0, 2) == 12);   // C2 8'
  CHECK(b3::wiredWheel(24, 0) == 24);  // C4 16' = C3
  CHECK(b3::wiredWheel(24, 2) == 36);  // C4 8'
  CHECK(b3::wiredWheel(24, 4) == 55);  // C4 2 2/3' = G5 (percussion)
  CHECK(b3::wiredWheel(60, 8) == 84);  // C7 1' folds down an octave
  for (int k = 0; k < 61; ++k) {
    for (int d = 0; d < 9; ++d) {
      const int w = b3::wiredWheel(k, d);
      CHECK(w >= 0 && w < b3::kNumWheels);
    }
  }
  // Every key/drawbar combination must land inside the generator.
  for (int n = b3::kLowestNote; n <= b3::kHighestNote; ++n) {
    for (int d = 0; d < b3::kNumDrawbars; ++d) {
      const b3::PartialMap m =
          b3::mapPartial(b3::midiToHz(n), b3::drawbarHarmonic(d));
      CHECK(m.wheel >= 0 && m.wheel < b3::kNumWheels);
    }
  }
}

void testSilence() {
  std::printf("[engine] silence and leakage\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p = cleanParams();
  p.drawbars.fill(0.0f);
  p.reverb = 0.0f;
  organ.setParams(p);
  auto r = render(organ, 0.6);
  CHECK(r.finite);
  CHECK(r.peak < 1e-4f);

  p.leakage = 1.0f;
  auto leak = render(organ, 0.6, &p);
  CHECK(leak.peak > 1e-5f);   // generator leakage is audible on an idle B-3
  CHECK(leak.peak < 0.05f);
}

void testPitchAndDrawbars() {
  std::printf("[engine] drawbar pitch and foldback\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p = cleanParams();
  organ.setParams(p);
  organ.noteOn(60, 1.0f);  // C4
  auto r = render(organ, 0.8);
  const size_t start = static_cast<size_t>(0.35 * kSr);
  const size_t len = static_cast<size_t>(0.35 * kSr);
  const double f8 = goertzel(r.left, start, len, 261.6256);
  const double f16 = goertzel(r.left, start, len, 130.8128);
  const double f4 = goertzel(r.left, start, len, 523.2511);
  CHECK(f8 > 0.02);
  CHECK(f8 > f16 * 8.0);
  CHECK(f8 > f4 * 8.0);
  organ.allNotesOff();

  organ.reset();
  p.drawbars = {{8, 0, 0, 0, 0, 0, 0, 0, 0}};
  organ.setParams(p);
  organ.noteOn(60, 1.0f);
  auto r16 = render(organ, 0.8);
  const double sub = goertzel(r16.left, start, len, 130.8128);
  const double uni = goertzel(r16.left, start, len, 261.6256);
  CHECK(sub > 0.02);
  CHECK(sub > uni * 6.0);
}

void testPercussion() {
  std::printf("[engine] percussion decay\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p = cleanParams();
  p.drawbars.fill(0.0f);
  p.percussionOn = true;
  p.percussionThird = true;
  p.percussionSlow = false;
  organ.setParams(p);
  organ.noteOn(60, 1.0f);
  auto r = render(organ, 0.8);
  const double early = goertzel(r.left, static_cast<size_t>(0.03 * kSr),
                                static_cast<size_t>(0.10 * kSr), 783.99);
  const double late = goertzel(r.left, static_cast<size_t>(0.55 * kSr),
                               static_cast<size_t>(0.15 * kSr), 783.99);
  CHECK(early > 1e-3);
  CHECK(late < early * 0.10);

  // Slow decay must still be ringing well after the fast one has died.
  b3::Organ slow;
  slow.prepare(kSr, 512);
  p.percussionSlow = true;
  slow.setParams(p);
  slow.noteOn(60, 1.0f);
  auto rs = render(slow, 0.8);
  const double slowLate = goertzel(rs.left, static_cast<size_t>(0.55 * kSr),
                                   static_cast<size_t>(0.15 * kSr), 783.99);
  CHECK(slowLate > late * 1.5);
}

void testScanner() {
  std::printf("[engine] scanner vibrato/chorus\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p = cleanParams();
  p.drawbars.fill(8.0f);
  organ.setParams(p);
  organ.noteOn(60, 1.0f);
  auto dry = render(organ, 0.6);

  p.scanner = 3;  // V3
  auto vib = render(organ, 1.2, &p);
  double diff = 0.0;
  const size_t n = std::min(dry.left.size(), vib.left.size());
  for (size_t i = 0; i < n; ++i) {
    const double d = dry.left[i] - vib.left[i];
    diff += d * d;
  }
  CHECK(vib.finite);
  const size_t tail = vib.left.size() > 20000 ? vib.left.size() - 20000 : 0;
  CHECK(rms(vib.left, tail, vib.left.size() - tail) > 0.005);
  CHECK(std::sqrt(diff / static_cast<double>(std::max<size_t>(n, 1))) > 1e-3);
}

void testRotary() {
  std::printf("[engine] Leslie rotor inertia\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p = cleanParams();
  p.drawbars.fill(8.0f);
  p.hornDrum = 0.5f;
  organ.setParams(p);
  organ.noteOn(60, 1.0f);

  // Fast: accumulate angle deltas while holding the note.
  auto accumulate = [&](double seconds) {
    double total = 0.0;
    float prev = organ.hornAngleDegrees();
    const int steps = static_cast<int>(seconds / 0.01);
    std::vector<float> l(480), r(480);
    for (int i = 0; i < steps; ++i) {
      std::fill(l.begin(), l.end(), 0.0f);
      std::fill(r.begin(), r.end(), 0.0f);
      organ.process(l.data(), r.data(), 480);
      const float cur = organ.hornAngleDegrees();
      double d = static_cast<double>(cur - prev);
      if (d < 0.0) d += 360.0;
      total += d;
      prev = cur;
    }
    return total;
  };

  p.leslieSpeed = 2;
  render(organ, 3.0, &p);  // let the rotor reach fast speed
  const double fastDelta = accumulate(1.0);
  p.leslieSpeed = 0;
  render(organ, 4.0, &p);  // brake
  const double stoppedDelta = accumulate(1.0);
  CHECK(fastDelta > 1800.0);       // ~6.6 rev/s
  CHECK(stoppedDelta < 360.0);     // spinning down, not driven
}

void testStability() {
  std::printf("[engine] stress: full registration, drive, polyphony\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p;
  p.drawbars.fill(8.0f);
  p.percussionOn = true;
  p.scanner = 6;
  p.leslieSpeed = 2;
  p.drive = 1.0f;
  p.keyClick = 1.0f;
  p.leakage = 1.0f;
  p.hum = 1.0f;
  p.reverb = 1.0f;
  p.volume = 1.0f;
  p.wear = 1.0f;
  organ.setParams(p);
  const int notes[12] = {36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79};
  for (int n : notes) organ.noteOn(n, 1.0f);
  auto r = render(organ, 4.0);
  CHECK(r.finite);
  CHECK(r.peak <= 1.001f);
  CHECK(r.peak > 0.1f);
  CHECK(organ.activeNotes() == 12);

  // Turn the room off, then confirm the keys actually release.
  p.reverb = 0.0f;
  organ.setParams(p);
  organ.allNotesOff();
  auto settle = render(organ, 0.4);
  (void)settle;
  auto off = render(organ, 1.2);
  CHECK(off.finite);
  CHECK(off.peak < 0.04f);
  CHECK(organ.activeNotes() == 0);
}

void testCpu() {
  std::printf("[perf] real-time factor\n");
  b3::Organ organ;
  organ.prepare(kSr, 512);
  b3::Params p;
  p.drawbars.fill(8.0f);
  p.scanner = 3;
  p.leslieSpeed = 2;
  p.drive = 0.5f;
  p.reverb = 0.4f;
  organ.setParams(p);
  for (int n : {48, 52, 55, 60, 64, 67}) organ.noteOn(n, 1.0f);

  const double seconds = 5.0;
  const auto t0 = std::chrono::steady_clock::now();
  auto r = render(organ, seconds);
  const auto t1 = std::chrono::steady_clock::now();
  const double elapsed =
      std::chrono::duration<double>(t1 - t0).count();
  const double rtf = seconds / elapsed;
  std::printf("  rendered %.1fs audio in %.3fs  (%.1fx realtime), peak %.3f\n",
              seconds, elapsed, rtf, r.peak);
  CHECK(r.finite);
  CHECK(rtf > 2.0);
}

}  // namespace

int main() {
  std::printf("B-3 engine host tests @ %.0f Hz\n\n", kSr);
  testModel();
  testSilence();
  testPitchAndDrawbars();
  testPercussion();
  testScanner();
  testRotary();
  testStability();
  testCpu();

  std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
