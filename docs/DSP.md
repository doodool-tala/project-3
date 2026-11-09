# DSP and physical-model notes

The engine is written to be explainable: every block implements a physical or
electrical behaviour, and the tests pin the behaviour that matters musically.
All sources are portable C++17; floats are used throughout the audio path.

## 1. Tonewheel generator

There are 91 wheels, index 0 = C1 at

```
f_w = 32.70319566 * 2^(w / 12)      Hz, w = 0..90
```

A wheel is a 2^12-entry wavetable oscillator with a 32-bit fixed-point phase
accumulator (`dsp::TableOsc`). Phase increments are derived from the wheel
frequency; a wheel never resets while the app runs, so a repeated key shares
the same running phase with every other key that uses that wheel.

The table is not a pure sine. A magnetic pickup sees the tooth as a
slightly asymmetric pulse, so the wheel carries small 2nd/3rd/5th harmonics.
Three tables (rich, medium, sine) are selected by wheel register to keep the
top octave from aliasing. Each wheel also has a fixed random trim (0.86–1.12)
and, with wear enabled, a slow ±2 cent eccentricity drift.

The bus output per wheel is

```
bus = Σ_w gain[w] * (wheel[w] + crosstalk * neighbours(w)) + hum
```

where `crosstalk` is a small fixed coupling to the adjacent wheels, and `hum`
is 60/120/180/240 Hz induced in the matching transformer. `gain[w]` comes from
the drawbars and key gates (below), plus a fixed low-level leakage bed. The bus
is softly saturated and scaled so a full 9-drawbar chord has headroom before
the preamp.

Implementation: `engine/src/tonewheel.cpp`.

## 2. Manual wiring, foldback and drawbars

The drawbar-to-wheel assignment is not computed from equal temperament. The
real instrument hard-wires each of the 61 keys to specific tonewheels; that
is why the bottom octave's 16' is folded up to unison and why the top octave's
higher drawbars fold down in an asymmetric pattern. `engine/include/b3/wiring.h`
contains the 61×9 service-manual table (wheel numbers 1–91), with `wiredWheel()`
returning a zero-based index.

Drawbar level law (0–8):

```
level(v) = smoothstep(v/8) * 1.30
```

Each bar also has a pickup taper (0.79–1.00) and a harmonic ratio relative to
8':

| bar | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pitch | 16' | 5⅓' | 8' | 4' | 2⅔' | 2' | 1⅗' | 1⅓' | 1' |
| ratio | ½ | 3/2 | 1 | 2 | 3 | 4 | 5 | 6 | 8 |

For a held key, the engine adds `gate * barLevel * taper` to each wired wheel's
bus gain. The gains are recomputed every 16-sample chunk and linearly
interpolated across the chunk, which keeps registration changes click-free.
Two keys that share a wheel simply add, exactly like the shared busbar.

`mapPartial()` also exists as a generic harmonic-to-wheel mapper with octave
foldback; it is used by tests and as a fallback where no wiring entry exists.

## 3. Key contacts and key click

Each key has a gate that rises in ~1.8 ms and falls in ~5 ms. Contact make and
break also inject a broadband click: a band-passed noise burst (centre follows
the note, 0.9–5.2 kHz) plus a short low body, with 2.2 ms and 8 ms envelopes.
Eight click voices are pooled, and the click joins the bus *before* the
scanner, as on the real wiring. The `CLICK` knob scales both make and break
clicks; the release click is quieter.

## 4. Percussion

Percussion is monophonic and single-triggered. On a new key it taps the wheel
wired to the 2⅔' bar (3rd harmonic) or the 4' bar (2nd harmonic), using the
physical wheel frequency rather than equal temperament. This keeps its beating
against the drawbar partials.

The envelope is a short attack (1.8 ms) followed by an exponential decay with
τ = 115 ms (fast) or 340 ms (slow), plus a 6 ms click. It bypasses the scanner
and is cut when the last key is released. Level is full or −6 dB (`SOFT`).

Implementation: `engine/src/percussion.cpp`.

## 5. Scanner vibrato / chorus

The scanner is modelled as a rotating tap on an LC delay line. The line delay
is 0.90 ms, modulated by up to 0.65 ms:

```
d(t) = sr * (0.00090 + 0.00065 * depth * (0.5 + 0.5 sin(2π f t)))
wet  = delayLine.readCubic(d(t))
wet *= 1 - am * (0.5 + 0.5 sin(2π f t + 0.9))
out  = dry + (wet - dry) * mix
```

with `f ≈ 6 Hz`, depth 0.30/0.55/0.85 for V1–V3 and 0.35/0.60/0.90 for
C1–C3, and `mix` 1.0 for vibrato and 0.55–0.62 for chorus. The additional
amplitude ripple represents the eight switching events per revolution. A
one-pole 9.5 kHz low-pass stands in for the line's losses. The scanner is
applied to the whole bus, but not to percussion.

Implementation: `engine/src/scanner.cpp`.

## 6. Tube preamp

The input transformer is a 28 Hz high-pass. The tube stage is

```
u = g * (x + bias)
y = log(cosh(u)) differentiated (ADAA-1), minus log(cosh(g*bias))
```

First-order antiderivative anti-aliasing evaluates the average of tanh between
consecutive samples, which keeps the asymmetric saturation from aliasing at
high drive. The ADAA state is initialised at the quiescent operating point and
the static DC is subtracted, so switching the amp on does not thump. The bias
(0.06–0.28) grows with drive and generates the even-order harmonics that make
the organ "growl".

A 210 Hz low shelf, a fixed −2.5 dB dip at 750 Hz and a 3.4 kHz high shelf form
the tone network; `TONE` moves both shelves ±7 dB. Output gain compensates for
the drive so the knob changes character more than loudness.

Implementation: `engine/src/drive.cpp`.

## 7. Leslie cabinet

Signal is split with a Linkwitz–Riley 4th-order crossover at 800 Hz, built from
two identical Butterworth biquads per side. The horn path and drum path have
their own delay lines and their own rotor geometry.

For a microphone at `(D, ±S/2)` and a rotor of radius `R` at angle θ, the
source is at `(R cosθ, R sinθ)` and

```
dist  = |mic - source|
delay = 0.004 + (dist - (D - R)) / c        c = 343 m/s
```

The derivative of that path length is the Doppler shift; at the tremolo speed
the horn rim moves at ≈7 m/s, giving roughly ±2% pitch, which is the familiar
Leslie warble. The horn radiates a cardioid, so its level at each mic is

```
dir = 0.22 + 0.78 * max(0, dot(axis, toMic))^1.6
```

and the same directivity controls a one-pole low-pass (2.2–15.7 kHz) that makes
the sound dull as the horn faces away. The drum uses a weaker figure-of-eight
pattern and a smaller radius. Three short reflections per path (3.1, 6.7,
11.3 ms) plus high-pass/low-pass/wood-resonance filters form the cabinet
response.

Geometry-dependent delay, gain and cutoff tables are precomputed over 512
angles per rotor, so the per-sample cost is a table lookup and a cubic
interpolation instead of trigonometry. Rotor speed approaches the target with
an exponential time constant: 0.6 s accelerating, 1.3 s coasting down, 2.1 s to
brake. Mechanical wind noise is proportional to speed squared and therefore
silent when stopped.

Implementation: `engine/src/leslie.cpp`.

## 8. Room reverb

An 8-line feedback delay network with prime-ish delays, a normalised Hadamard
feedback matrix, one-pole damping in each line, 18 ms pre-delay, and RT60 from
0.55 s to 5.35 s. The wet output is derived from two different sign patterns of
the line outputs for stereo width. The reverb sits after the Leslie so the
rotor stays immediate.

Implementation: `engine/src/reverb.cpp`.

## 9. Output

Expression is applied before the Leslie (as the pedal acts on the preamp
output). Master volume is applied after the reverb. A 9 Hz DC blocker removes
any residual offset, and a feed-forward limiter (1.2 ms attack, 150 ms
release, 0.88 threshold) plus a final `softClip` guarantees the output cannot
leave ±1.0. Per-block finite checks protect against denormal/NaN surprises.

## 10. Threading and parameter flow

`Organ::noteOn/noteOff/setParams` are called from the UI or MIDI thread and only
push into a small guarded queue/state snapshot. The audio thread drains that
state at the start of each callback, applies parameter changes, and runs the
16-sample chunk loop. UI telemetry (rotor angles, scanner angle, peak, active
notes) is published via relaxed atomics. This keeps allocation, locks and
expensive work out of the sample loop.

## 11. What is deliberately not modelled

* One 61-key upper manual only; no lower manual or 25-note pedalboard.
* Wheel frequencies are equal-tempered with mechanical trim, not a gear-tooth
  table.
* The scanner is a rotating delay tap, not a full switched-capacitor network.
* The Leslie's cabinet diffraction is approximated with short reflections and
  filters rather than a boundary-element model.
* No drawbar preset keys or user preset storage (six fixed registrations are
  provided).
* Speaker/room convolution is replaced by the FDN so the APK stays small and
  fully generative.

These are the main differences from a full emulation such as Arturia's; the
generator, harness, percussion, scanner, preamp and rotary speaker are all
modelled explicitly rather than sampled.

## 12. On-device execution evidence

The debug APK was installed and launched on a headless Android 11 (API 30)
x86_64 emulator with software rendering and no hardware acceleration. The
custom view drew the 1800×1000 panel at 1280×800, the JNI bridge received a
synthesized C3 (`noteOn 48`), and Oboe opened a 48 kHz stereo AAudio stream.
While the key was tapped, the native callback measured `level peak=0.0476`,
compared with about `0.002` from the idle hum/leakage bed, so the output path
is demonstrably non-silent rather than assumed.

The committed screenshot is `docs/emulator-panel.png`; an exact log excerpt and
the emulator configuration are in `docs/emulator-evidence.txt`. The permanent
`tools/emulator_smoke.sh` check and its `emulator-smoke` CI job repeat the
install, launch, C3 injection, panel-draw and audio-level assertions.

This evidence covers the Java UI → JNI → `b3::Organ` → Oboe path on an emulated
Android device. It does not establish physical touchscreen latency or feel,
real hardware audio latency, USB/Bluetooth MIDI behavior, or behavior on a
physical phone. The unaccelerated system UI also produced ANR dialogs during
boot; those were system-level, while the app process and its audio callback
remained alive.
