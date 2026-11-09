// Musical model constants and pure helper functions of the tonewheel organ.
#pragma once

namespace b3 {

constexpr int kNumDrawbars = 9;
constexpr int kNumWheels = 91;
// B-3 manual compass: 61 keys, C2 (65.4 Hz) to C7 (2092 Hz).
constexpr int kNumKeys = 61;
constexpr int kLowestNote = 36;   // MIDI C2
constexpr int kHighestNote = 96;  // MIDI C7

// Wheel 0 is the lowest generator wheel (C1). The Hammond generator spans
// 91 semitone-related wheels, roughly 32.7 Hz to 5.9 kHz. The real generator
// is geared from the synchronous motor; we model the resulting temperament
// and add per-wheel mechanical trim separately in the tonewheel bank.
constexpr double kWheelBaseHz = 32.70319566257483;

double midiToHz(double midi);
double wheelFrequency(int wheel);

// Drawbar harmonic ratios relative to 8' (unison):
//   16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
double drawbarHarmonic(int index);
float drawbarTaper(int index);
const char* drawbarLabel(int index);

struct PartialMap {
  int wheel;           // generator wheel selected by the busbar wiring
  int octavesFolded;   // >0 folded up, <0 folded down (classic B-3 foldback)
};

// Map a note frequency plus harmonic ratio onto a physical generator wheel.
// Notes outside the 91-wheel range fold by octaves, exactly like the B-3
// harness wiring, which is why the extreme drawbars change timbre instead of
// simply running out of wheels.
PartialMap mapPartial(double noteHz, double harmonicRatio);

}  // namespace b3
