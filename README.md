# AMRM — AM / Ring Modulation Oscillator

A two-operator **amplitude- and ring-modulation** oscillator for the
**Korg minilogue xd** (logue SDK v1, `osc`).

A **carrier** plays the note. An internal **modulator**, running at a tunable
frequency ratio, modulates it like a polarizer (think Mutable Instruments
*Blinds*). A **dry/wet crossfader** blends the untouched carrier with the
modulated signal, so the sound never drops out completely.

```
c   = carrier(phase_c)                       // -1..1
m   = modulator(phase_m)                     // -1..1
wet = c * ((1 - L) * 0.5 * (1 + m) + L * m)  // L=0: AM, L=1: ring mod
y   = (1 - T) * c + T * wet                  // T=0: dry carrier, T=1: full effect
```

- **L (Level)** sweeps from **AM** (the fundamental survives) to **ring
  modulation** (the fundamental is cancelled).
- **T (Depth)** is a dry/wet mix — at `0` you hear the pure carrier, never
  silence.
- Because `|wet| ≤ 1`, no output normalisation is needed.

## Controls

| Control         | Default (Swap = 1) | Swapped (Swap = 2) |
|-----------------|--------------------|--------------------|
| **SHAPE** (+LFO)| **L** — Level      | **T** — Depth      |
| **SHIFT+SHAPE** | **T** — Depth      | **L** — Level      |

The LFO always rides on **SHAPE**. Both Level and Depth are one-pole smoothed,
so knob steps and LFO never click.

## Edit menu

The minilogue xd shows typeless values offset by one, so an internal `0`
displays as **"1"**. The table lists the **displayed** numbers.

| # | Parameter      | Values (display)        | Meaning |
|---|----------------|-------------------------|---------|
| 1 | **Swap**       | 1–2                     | 1 = normal · 2 = swap SHAPE / SHIFT+SHAPE |
| 2 | **Carrier**    | 1–5                     | 1 sine · 2 triangle · 3 parabola · 4 saw · 5 square (default **2**) |
| 3 | **Mod Wave**   | 1–6                     | 1 sine · 2 triangle · 3 parabola · 4 saw · 5 square · 6 noise (default **1**) |
| 4 | **Mod Ratio**  | 1–12                    | frequency factor `0.25, 0.5, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16` (default **3** = ×1) |
| 5 | **Mod Detune** | −100…100 %              | modulator fine detune, ±10 Hz at the extremes, fine around 0 |

Notes:

- The modulator tracks the keyboard and pitch bend. Negative modulator
  frequencies are allowed (**through-zero**) and the phase wraps cleanly in
  both directions.
- **Noise** (Mod Wave = 6) ignores Ratio and Detune.
- The band-limited waves (parabola / saw / square) use the SDK's anti-aliased
  tables, and the modulator's contribution is faded out between 18 and 22 kHz
  to keep high ratios clean (noise is exempt).

## Install

Grab `amrm.mnlgxdunit` from the [Releases](../../releases) page, drop it onto a
free *User Oscillator* slot in the **minilogue xd Sound Librarian**, and transfer
it to the instrument. Firmware ≥ 2.00 is required.

On the device:

1. Set **MULTI ENGINE** to **USR** and pick **AMRM** with TYPE. Turn VCO1/VCO2
   down and MULTI up to hear it on its own.
2. To make the LFO reach the effect, set **LFO TARGET = SHAPE** and, in
   **Program Edit**, **"LFO Target OSC" = Multi** (otherwise the LFO modulates
   VCO1/2 instead of AMRM).
3. Optional: **Program Edit → "Multi Routing" = Post VCF** so the multi-engine
   output is not coloured by the VCF.

## Build

Requires the [logue SDK](https://github.com/korginc/logue-sdk) with submodules
and the Docker build image. Clone this repo into the platform folder:

```sh
git clone <this-repo> logue-sdk/platform/minilogue-xd/AMRM
cd logue-sdk
docker/run_cmd.sh build minilogue-xd/AMRM
```

The resulting `amrm.mnlgxdunit` is written into the project folder.

## Credits

Built with the Korg logue SDK (BSD 3-Clause). Carrier/modulator waveforms,
band-limited tables and math helpers are from the SDK's oscillator API.
