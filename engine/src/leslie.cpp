#include "leslie.h"

#include <cmath>

namespace b3 {
namespace {
constexpr double kTwoPiD = 6.28318530717958647692;
constexpr double kSpeedOfSound = 343.0;  // m/s at 20 C
constexpr double kHornRadius = 0.165;    // m
constexpr double kDrumRadius = 0.135;    // m
constexpr float kReflectionGain[3] = {0.20f, 0.11f, 0.065f};
constexpr double kReflectionTime[3] = {0.0031, 0.0067, 0.0113};
}  // namespace

void Leslie::prepare(double sampleRate) {
  sr_ = sampleRate;
  hornLine_.resize(static_cast<int>(0.06 * sr_));
  drumLine_.resize(static_cast<int>(0.06 * sr_));
  hornHp1_.highpass(sr_, 800.0, 0.7071);
  hornHp2_.highpass(sr_, 800.0, 0.7071);
  drumLp1_.lowpass(sr_, 800.0, 0.7071);
  drumLp2_.lowpass(sr_, 800.0, 0.7071);
  cabHp_.highpass(sr_, 75.0, 0.6);
  cabPeak_.peak(sr_, 210.0, 0.8, 2.5);
  cabLp_.lowpass(sr_, 11500.0, 0.7);
  predelaySamples_ = static_cast<float>(0.012 * sr_);
  rebuildLuts();
  reset();
}

void Leslie::reset() {
  hornLine_.clear();
  drumLine_.clear();
  hornHp1_.reset(); hornHp2_.reset();
  drumLp1_.reset(); drumLp2_.reset();
  cabHp_.reset(); cabPeak_.reset(); cabLp_.reset();
  lpL_.z = lpR_.z = lpDrumL_.z = lpDrumR_.z = 0.0f;
  speed_ = 0.0f;
  hornAngle_ = 0.0;
  drumAngle_ = 0.0;
  noiseZ_ = 0.0f;
}

void Leslie::setParams(float micDistance01, float micSpread01, float hornDrum01,
                       float cabinet01, int speed) {
  const bool rebuild = std::fabs(micDistance01 - micDistance_) > 1e-4f ||
                       std::fabs(micSpread01 - micSpread_) > 1e-4f;
  micDistance_ = dsp::clampf(micDistance01, 0.0f, 1.0f);
  micSpread_ = dsp::clampf(micSpread01, 0.0f, 1.0f);
  hornDrum_ = dsp::clampf(hornDrum01, 0.0f, 1.0f);
  cabinet_ = dsp::clampf(cabinet01, 0.0f, 1.0f);
  speedMode_ = speed;

  hornLevel_ = 0.35f + 1.0f * hornDrum_;
  drumLevel_ = 1.35f - 1.0f * hornDrum_;
  // Mic distance also darkens the cabinet: farther mics lose air HF.
  const double lpHz = 14500.0 - static_cast<double>(micDistance_) * 6500.0;
  cabLp_.lowpass(sr_, lpHz, 0.7);

  if (rebuild) rebuildLuts();

  targetSpeed_ = speedMode_ == 2 ? 6.6f : (speedMode_ == 1 ? 0.78f : 0.0f);
  (void)cabinet_;
}

void Leslie::rebuildLuts() {
  const double distM = 0.35 + 1.9 * static_cast<double>(micDistance_);
  const double spreadM = 0.15 + 0.95 * static_cast<double>(micSpread_);
  const double micL[2][2] = {{distM, spreadM * 0.5}, {distM, -spreadM * 0.5}};
  const double minDist = distM - kHornRadius;

  for (int mic = 0; mic < 2; ++mic) {
    for (int i = 0; i < kLut; ++i) {
      const double th = kTwoPiD * static_cast<double>(i) / static_cast<double>(kLut);
      const double cosT = std::cos(th), sinT = std::sin(th);

      // ---- horn -------------------------------------------------------
      {
        const double sx = kHornRadius * cosT;
        const double sy = kHornRadius * sinT;
        const double dx = micL[mic][0] - sx;
        const double dy = micL[mic][1] - sy;
        const double d = std::sqrt(dx * dx + dy * dy);
        const double rel = (d - minDist) / kSpeedOfSound;
        const double toMicX = dx / d, toMicY = dy / d;
        const double dot = cosT * toMicX + sinT * toMicY;
        const double front = std::max(0.0, dot);
        const double dir = 0.22 + 0.78 * std::pow(front, 1.6);
        const double falloff = 1.0 / std::max(0.55, d);
        hornDelay_[mic][i] = static_cast<float>(0.004 + rel) ;
        hornGain_[mic][i] = static_cast<float>(dir * falloff * 0.85);
        hornCut_[mic][i] = static_cast<float>(0.20 + 0.80 * front);
      }
      // ---- bass drum --------------------------------------------------
      {
        const double sx = kDrumRadius * cosT;
        const double sy = kDrumRadius * sinT;
        const double dx = micL[mic][0] - sx;
        const double dy = micL[mic][1] - sy;
        const double d = std::sqrt(dx * dx + dy * dy);
        const double rel = (d - (distM - kDrumRadius)) / kSpeedOfSound;
        const double toMicX = dx / d, toMicY = dy / d;
        const double dot = std::fabs(cosT * toMicX + sinT * toMicY);
        // The drum radiates through the front and back louvres.
        const double dir = 0.55 + 0.45 * dot;
        const double falloff = 1.0 / std::max(0.55, d);
        drumDelay_[mic][i] = static_cast<float>(0.004 + rel);
        drumGain_[mic][i] = static_cast<float>(dir * falloff * 0.80);
        drumCut_[mic][i] = static_cast<float>(0.35 + 0.65 * dot);
      }
    }
  }
}

void Leslie::updateCoeffs() {
  // Interpolate the directivity-dependent absorption at the current angle.
  auto lut = [&](const float* table, double angle) {
    double a = angle / kTwoPiD;
    a -= std::floor(a);
    const double x = a * kLut;
    const int i0 = static_cast<int>(x) % kLut;
    const int i1 = (i0 + 1) % kLut;
    const float f = static_cast<float>(x - std::floor(x));
    return table[i0] + (table[i1] - table[i0]) * f;
  };
  const float hL = lut(hornCut_[0], hornAngle_);
  const float hR = lut(hornCut_[1], hornAngle_);
  const float dL = lut(drumCut_[0], drumAngle_);
  const float dR = lut(drumCut_[1], drumAngle_);
  const float twoPi = 6.2831853f;
  lpL_.a = 1.0f - std::exp(-twoPi * (2200.0f + 13500.0f * hL) / static_cast<float>(sr_));
  lpR_.a = 1.0f - std::exp(-twoPi * (2200.0f + 13500.0f * hR) / static_cast<float>(sr_));
  lpDrumL_.a = 1.0f - std::exp(-twoPi * (3000.0f + 9000.0f * dL) / static_cast<float>(sr_));
  lpDrumR_.a = 1.0f - std::exp(-twoPi * (3000.0f + 9000.0f * dR) / static_cast<float>(sr_));
}

void Leslie::process(const float* monoIn, float* left, float* right, int frames) {
  // Rotor inertia: exponential approach to the selected speed, faster to
  // accelerate than to coast down, even slower to brake.
  const float dt = static_cast<float>(frames) / static_cast<float>(sr_);
  const float tau = targetSpeed_ > speed_ ? 0.60f : (targetSpeed_ < 0.02f ? 2.1f : 1.3f);
  speed_ += (targetSpeed_ - speed_) * (1.0f - std::exp(-dt / tau));
  const double omega = kTwoPiD * static_cast<double>(speed_);

  updateCoeffs();

  for (int i = 0; i < frames; ++i) {
    const float x = monoIn[i];
    // 2-way Linkwitz-Riley crossover at 800 Hz.
    float horn = hornHp1_.process(x);
    horn = hornHp2_.process(horn);
    float drum = drumLp1_.process(x);
    drum = drumLp2_.process(drum);
    hornLine_.write(horn);
    drumLine_.write(drum);

    // Rotary mechanical noise: wind rises with rotor speed and is silent
    // when the cabinet is braked, so an idle organ has no noise floor.
    const float spin = speed_ / 6.6f;
    const float wind = (rng_.bipolar() * 0.35f + noiseZ_ * 0.65f) *
                      0.0016f * spin * spin;
    noiseZ_ += 0.35f * (rng_.bipolar() - noiseZ_);

    float outL = 0.0f, outR = 0.0f;
    float* chans[2] = {&outL, &outR};
    for (int mic = 0; mic < 2; ++mic) {
      const double angleH = hornAngle_ + (mic ? 0.0 : 0.0);
      const double angleD = drumAngle_;
      // Look up geometry at the rotor angle. Same LUT for both mics but
      // different entries, so no per-sample trigonometry is required.
      auto sampleLut = [&](const float* table, double angle) {
        double a = angle / kTwoPiD;
        a -= std::floor(a);
        const double xf = a * kLut;
        const int i0 = static_cast<int>(xf) % kLut;
        const int i1 = (i0 + 1) % kLut;
        const float f = static_cast<float>(xf - std::floor(xf));
        return table[i0] + (table[i1] - table[i0]) * f;
      };

      const float hd = sampleLut(hornDelay_[mic], angleH);
      const float hg = sampleLut(hornGain_[mic], angleH);
      const float dd = sampleLut(drumDelay_[mic], angleD);
      const float dg = sampleLut(drumGain_[mic], angleD);

      float h = hornLine_.readCubic(hd * static_cast<float>(sr_));
      h = (mic ? lpR_ : lpL_).process(h);
      // Three cabinet reflections per path.
      for (int r = 0; r < 3; ++r) {
        h += kReflectionGain[r] * hornLine_.readCubic(
                 (hd + static_cast<float>(kReflectionTime[r])) *
                 static_cast<float>(sr_));
      }

      float d = drumLine_.readCubic(dd * static_cast<float>(sr_));
      d = (mic ? lpDrumR_ : lpDrumL_).process(d);
      for (int r = 0; r < 3; ++r) {
        d += kReflectionGain[r] * 0.8f * drumLine_.readCubic(
                 (dd + static_cast<float>(kReflectionTime[r] * 1.2)) *
                 static_cast<float>(sr_));
      }

      *chans[mic] += h * hg * hornLevel_ * hornTrim_ +
                     d * dg * drumLevel_ * drumTrim_ + wind;
    }

    // Cabinet / room colouration.
    outL = cabLp_.process(cabPeak_.process(cabHp_.process(outL)));
    outR = cabLp_.process(cabPeak_.process(cabHp_.process(outR)));

    // The two mics see slightly different arrival times from the cabinet;
    // a short cross delay widens the image without smearing the rotor.
    left[i] = outL * 0.9f;
    right[i] = outR * 0.9f;

    hornAngle_ += omega;
    if (hornAngle_ >= kTwoPiD) hornAngle_ -= kTwoPiD;
    drumAngle_ += omega * 1.0;
    if (drumAngle_ >= kTwoPiD) drumAngle_ -= kTwoPiD;
  }
}

}  // namespace b3
