/*
 * amrm.cpp
 *
 * AM / Ring-Modulation oscillator for the minilogue xd (logue SDK v1).
 *
 * A carrier plays the note; an internal modulator at a tunable frequency ratio
 * modulates it like a polarizer (think Mutable Instruments Blinds). A dry/wet
 * crossfader blends the untouched carrier with the modulated signal, so the
 * sound never drops out completely.
 *
 * Per sample:
 *   c   = carrier(phase_c)                       // -1..1
 *   m   = modulator(phase_m)                     // -1..1
 *   wet = c * ((1 - L) * 0.5 * (1 + m) + L * m)  // L=0: AM, L=1: ring mod
 *   y   = (1 - T) * c + T * wet                  // T=0: dry carrier, T=1: full effect
 *
 * |wet| <= 1, so no extra normalisation is needed.
 *
 * Panel (Swap = 0, default):
 *   SHAPE (LFO) = L (Level) : 0 = AM (fundamental stays) -> 1 = ring mod (fundamental gone)
 *   SHIFT+SHAPE = T (Depth) : 0 = dry carrier -> 1 = full effect
 *   Swap = 1 swaps the two knobs. The LFO always rides on SHAPE.
 *
 * Edit params: Swap, Carrier wave, Mod wave, Mod ratio, Mod detune (see OSC_PARAM).
 */

#include "userosc.h"

// ---- edit-parameter / knob state --------------------------------------------
static float   s_shape     = 0.f;   // SHAPE position [0,1] (LFO is added on top)
static float   s_shift     = 0.f;   // SHIFT+SHAPE position [0,1]
static uint8_t s_swap      = 0;     // 0: SHAPE=L, SHIFT+SHAPE=T | 1: swapped
static uint8_t s_carrier   = 1;     // 0 sin, 1 tri, 2 par, 3 saw, 4 sqr
static uint8_t s_mod_wave  = 0;     // 0 sin, 1 tri, 2 par, 3 saw, 4 sqr, 5 noise
static float   s_ratio     = 1.f;   // modulator frequency factor
static float   s_detune_hz = 0.f;   // modulator detune offset in Hz

// ---- running state ----------------------------------------------------------
static float s_phase_c = 0.f;
static float s_phase_m = 0.f;
static float s_l_sm    = 0.f;       // smoothed Level
static float s_t_sm    = 0.f;       // smoothed Depth

// Modulator ratio table (index 0..11). Only integers and sub-octaves.
static const float k_ratios[12] = {
  0.25f, 0.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 12.f, 16.f
};

#define AMRM_SMOOTH  (0.01f)        // one-pole coef for L/T (~2 ms, click-free)

// Band-limit table index [0,6] for the band-limited waves (par/saw/sqr).
__fast_inline float amrm_blidx(uint8_t wav, float note)
{
  switch (wav) {
  case 2:  return osc_bl_par_idx(note);
  case 3:  return osc_bl_saw_idx(note);
  case 4:  return osc_bl_sqr_idx(note);
  default: return 0.f; // sine / triangle: unused
  }
}

// One oscillator sample. phase in [0,1), blidx precomputed for the block.
__fast_inline float amrm_wave(uint8_t wav, float phase, float blidx)
{
  switch (wav) {
  case 1:  return (phase < 0.5f) ? (4.f * phase - 1.f) : (3.f - 4.f * phase); // triangle
  case 2:  return osc_bl2_parf(phase, blidx);
  case 3:  return osc_bl2_sawf(phase, blidx);
  case 4:  return osc_bl2_sqrf(phase, blidx);
  default: return osc_sinf(phase); // 0: sine
  }
}

void OSC_INIT(uint32_t platform, uint32_t api)
{
  (void)platform;
  (void)api;
  s_shape     = 0.f;
  s_shift     = 0.f;
  s_swap      = 0;
  s_carrier   = 1;   // triangle
  s_mod_wave  = 0;   // sine
  s_ratio     = 1.f;
  s_detune_hz = 0.f;
  s_phase_c   = 0.f;
  s_phase_m   = 0.f;
  s_l_sm      = 0.f;
  s_t_sm      = 0.f;
}

void OSC_CYCLE(const user_osc_param_t * const params,
               int32_t *yn,
               const uint32_t frames)
{
  const uint8_t note = (params->pitch) >> 8;

  // Carrier phase increment (cycles/sample) and carrier frequency in Hz.
  const float w_c = osc_w0f_for_note(note, params->pitch & 0xFF);
  const float f_c = w_c * (float)k_samplerate;

  const uint8_t car_wav = s_carrier;
  const uint8_t mod_wav = s_mod_wave;
  const bool    noise   = (mod_wav == 5);

  // Modulator frequency: follows the keyboard/pitch bend, plus a small detune.
  // Phase wrapping stays sign-safe (si_floorf) even if f_m ever goes negative.
  const float f_m = noise ? 0.f : (f_c * s_ratio + s_detune_hz);
  const float w_m = f_m * k_samplerate_recipf;

  // Band-limit indices, chosen once per block from each effective note.
  const float car_idx = amrm_blidx(car_wav, (float)note);
  float mod_note = (float)note;
  if (!noise && s_ratio > 0.f)
    mod_note = clipminmaxf(0.f, note + 12.f * fastlog2f(s_ratio), 127.f);
  const float mod_idx = amrm_blidx(mod_wav, mod_note);

  // Anti-aliasing: fade the modulator's share out between 18 and 22 kHz
  // (full passband below, silent above). Noise is broadband, so no fade.
  const float abs_fm = si_fabsf(f_m);
  const float mg = noise ? 1.f : clip01f((22000.f - abs_fm) * (1.f / 4000.f));

  // SHAPE gets the LFO added; Swap decides whether SHAPE drives L or T.
  const float shape = clip01f(s_shape + q31_to_f32(params->shape_lfo));
  const float l_tgt = s_swap ? s_shift : shape;
  const float t_tgt = s_swap ? shape   : s_shift;

  float phase_c = s_phase_c;
  float phase_m = s_phase_m;
  float l_sm = s_l_sm;
  float t_sm = s_t_sm;

  q31_t * __restrict y = (q31_t *)yn;
  const q31_t * y_e = y + frames;

  for (; y != y_e; ) {
    // Smooth Level and Depth so knob steps and the block-rate LFO don't click.
    l_sm += AMRM_SMOOTH * (l_tgt - l_sm);
    t_sm += AMRM_SMOOTH * (t_tgt - t_sm);

    const float c = amrm_wave(car_wav, phase_c, car_idx);
    float m = noise ? osc_white() : amrm_wave(mod_wav, phase_m, mod_idx);
    m *= mg;

    const float wet = c * ((1.f - l_sm) * 0.5f * (1.f + m) + l_sm * m);
    const float out = (1.f - t_sm) * c + t_sm * wet;
    *(y++) = f32_to_q31(0.9f * clip1m1f(out));

    phase_c += w_c;
    phase_c -= si_floorf(phase_c);
    phase_m += w_m;
    phase_m -= si_floorf(phase_m); // through-zero safe for negative w_m
  }

  s_phase_c = phase_c;
  s_phase_m = phase_m;
  s_l_sm    = l_sm;
  s_t_sm    = t_sm;
}

void OSC_NOTEON(const user_osc_param_t * const params)
{
  (void)params;
  s_phase_c = 0.f;
  s_phase_m = 0.f;
}

void OSC_NOTEOFF(const user_osc_param_t * const params)
{
  (void)params;
}

void OSC_PARAM(uint16_t index, uint16_t value)
{
  switch (index) {
  case k_user_osc_param_shape:
    s_shape = param_val_to_f32(value); // 0..1
    break;
  case k_user_osc_param_shiftshape:
    s_shift = param_val_to_f32(value); // 0..1
    break;
  case k_user_osc_param_id1: // Swap: 0 normal, 1 swapped
    s_swap = (value != 0) ? 1 : 0;
    break;
  case k_user_osc_param_id2: // Carrier wave 0..4
    s_carrier = (value > 4) ? 4 : (uint8_t)value;
    break;
  case k_user_osc_param_id3: // Mod wave 0..5
    s_mod_wave = (value > 5) ? 5 : (uint8_t)value;
    break;
  case k_user_osc_param_id4: // Mod ratio index 0..11
    s_ratio = k_ratios[(value > 11) ? 11 : value];
    break;
  case k_user_osc_param_id5:
    // Unipolar percent: value arrives 0..100. A fresh patch loads edit params at
    // their minimum on the minilogue xd, so detune is unipolar with 0 % = no
    // detune (neutral on load). Quadratic curve -> fine near 0, +10 Hz at 100 %.
    {
      const float d = clip01f(value * 0.01f);
      s_detune_hz = 10.f * d * d;
    }
    break;
  default:
    break;
  }
}
