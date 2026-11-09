// Offline demo: performs a short B-3 piece through the real-time engine and
// writes a 16-bit stereo WAV. Used by CI as a sonic smoke test / artifact.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "b3/b3.h"

namespace {

constexpr int kSampleRate = 48000;

void put32(std::vector<unsigned char>& b, unsigned v) {
  b.push_back(static_cast<unsigned char>(v & 0xff));
  b.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
  b.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
  b.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
}
void put16(std::vector<unsigned char>& b, unsigned v) {
  b.push_back(static_cast<unsigned char>(v & 0xff));
  b.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
}

bool writeWav(const std::string& path, const std::vector<float>& l,
              const std::vector<float>& r) {
  const unsigned frames = static_cast<unsigned>(std::min(l.size(), r.size()));
  const unsigned dataBytes = frames * 2 * 2;
  std::vector<unsigned char> h;
  h.insert(h.end(), {'R', 'I', 'F', 'F'});
  put32(h, 36 + dataBytes);
  h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put32(h, 16);
  put16(h, 1);        // PCM
  put16(h, 2);        // stereo
  put32(h, kSampleRate);
  put32(h, kSampleRate * 2 * 2);
  put16(h, 4);
  put16(h, 16);
  h.insert(h.end(), {'d', 'a', 't', 'a'});
  put32(h, dataBytes);

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fwrite(h.data(), 1, h.size(), f);
  std::vector<short> pcm(frames * 2);
  for (unsigned i = 0; i < frames; ++i) {
    auto q = [](float v) {
      v = std::max(-1.0f, std::min(1.0f, v));
      return static_cast<short>(std::lrint(v * 32767.0f));
    };
    pcm[2 * i] = q(l[i]);
    pcm[2 * i + 1] = q(r[i]);
  }
  std::fwrite(pcm.data(), sizeof(short), pcm.size(), f);
  std::fclose(f);
  return true;
}

struct Rig {
  b3::Organ organ;
  b3::Params p;
  std::vector<float> left;
  std::vector<float> right;
  float peak = 0.0f;

  void init() {
    organ.prepare(kSampleRate, 512);
    organ.setParams(p);
  }
  void apply() { organ.setParams(p); }

  void run(double seconds) {
    const int total = static_cast<int>(seconds * kSampleRate);
    const int block = 192;
    std::vector<float> l(block), r(block);
    int done = 0;
    while (done < total) {
      const int n = std::min(block, total - done);
      std::fill(l.begin(), l.end(), 0.0f);
      std::fill(r.begin(), r.end(), 0.0f);
      organ.process(l.data(), r.data(), n);
      for (int i = 0; i < n; ++i) {
        left.push_back(l[static_cast<size_t>(i)]);
        right.push_back(r[static_cast<size_t>(i)]);
        peak = std::max(peak, std::max(std::fabs(l[static_cast<size_t>(i)]),
                                       std::fabs(r[static_cast<size_t>(i)])));
      }
      done += n;
    }
  }

  void chord(const std::vector<int>& notes, double seconds,
             const std::vector<int>& release = {}) {
    for (int n : notes) organ.noteOn(n, 1.0f);
    run(seconds);
    const std::vector<int>& off = release.empty() ? notes : release;
    for (int n : off) organ.noteOff(n);
  }
};

const std::vector<int> kCmaj7 = {48, 52, 55, 59};
const std::vector<int> kAm7 = {45, 48, 52, 55};
const std::vector<int> kFmaj7 = {41, 45, 48, 52};
const std::vector<int> kG7 = {43, 47, 50, 53};
const std::vector<int> kCend = {36, 48, 52, 55, 60};

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "b3_demo.wav";
  Rig rig;
  rig.p.drawbars = {{8, 8, 8, 0, 0, 0, 0, 0, 0}};
  rig.p.percussionOn = true;
  rig.p.percussionThird = true;
  rig.p.percussionSlow = false;
  rig.p.scanner = 4;  // chorus C1
  rig.p.leslieSpeed = 1;
  rig.p.drive = 0.22f;
  rig.p.keyClick = 0.55f;
  rig.p.reverb = 0.25f;
  rig.p.volume = 0.78f;
  rig.p.micDistance = 0.45f;
  rig.init();

  std::printf("rendering demo performance...\n");

  // --- Ballad registration, chorus, slow rotor ---------------------------
  rig.chord(kCmaj7, 2.4);
  rig.run(0.06);
  rig.chord(kAm7, 2.4);
  rig.run(0.06);
  rig.chord(kFmaj7, 2.4);
  rig.run(0.06);
  rig.chord(kG7, 2.4);
  rig.run(0.06);

  // --- Open up: full drawbars, percussion, fast rotor --------------------
  rig.p.drawbars = {{8, 8, 8, 8, 8, 8, 0, 0, 0}};
  rig.p.percussionOn = true;
  rig.p.percussionThird = false;
  rig.p.scanner = 2;  // V2
  rig.p.leslieSpeed = 2;
  rig.p.drive = 0.38f;
  rig.p.reverb = 0.30f;
  rig.apply();

  rig.chord({48, 52, 55}, 0.7);
  rig.chord({55, 59, 62}, 0.7);
  rig.chord({53, 57, 60}, 0.7);
  rig.chord({48, 52, 55, 60}, 2.2);

  rig.p.drawbars = {{8, 8, 8, 8, 8, 8, 8, 8, 8}};
  rig.p.percussionOn = false;
  rig.p.scanner = 6;  // C3
  rig.apply();
  rig.chord({43, 48, 52, 55}, 1.8);

  // --- Brake, quiet hymn registration ------------------------------------
  rig.p.drawbars = {{8, 0, 8, 0, 0, 0, 0, 0, 8}};
  rig.p.scanner = 1;
  rig.p.leslieSpeed = 0;
  rig.p.drive = 0.12f;
  rig.p.reverb = 0.45f;
  rig.p.volume = 0.70f;
  rig.apply();
  rig.run(1.4);  // let the rotor coast down
  rig.chord({41, 48, 53, 57}, 3.2);
  rig.run(0.08);
  rig.chord({43, 50, 55, 58}, 3.0);

  // --- Final: full organ, drive, fast rotor, long ring-out --------------
  rig.p.drawbars = {{8, 8, 8, 8, 8, 8, 8, 8, 8}};
  rig.p.percussionOn = true;
  rig.p.percussionThird = true;
  rig.p.percussionSlow = true;
  rig.p.scanner = 3;
  rig.p.leslieSpeed = 2;
  rig.p.drive = 0.58f;
  rig.p.reverb = 0.35f;
  rig.p.volume = 0.8f;
  rig.apply();
  rig.chord(kCend, 4.0);
  rig.run(3.0);

  if (!writeWav(path, rig.left, rig.right)) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return 1;
  }
  std::printf("wrote %s: %zu frames (%.1fs), peak %.3f\n", path.c_str(),
              rig.left.size(),
              static_cast<double>(rig.left.size()) / kSampleRate, rig.peak);
  return 0;
}
