/*
 * Voice effects for the Linux engine.
 * Classic stages ported from ESP/Filters.cpp (biquads, ring mod, reverb comb,
 * volume, pitch, soft clip), plus the character stages in character.cpp.
 * Runs per channel in float; the full chain order is documented in dsp.h.
 */

#include "dsp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const unsigned int kPitchBufLen = 4096; /* power of two */
const float kPitchWindowSec = 0.020f;
const float kCombMaxSec = 0.300f;

#define FX_FIELD(kind, name) {#name, kind, offsetof(FxParams, name)}

const FxField kFields[] = {
    FX_FIELD(kFxBool, enabled),
    FX_FIELD(kFxFloat, volume_db),
    FX_FIELD(kFxBool, pitch_on),
    FX_FIELD(kFxFloat, pitch_semitones),
    FX_FIELD(kFxBool, hp_on),
    FX_FIELD(kFxFloat, hp_freq),
    FX_FIELD(kFxInt, hp_cascade),
    FX_FIELD(kFxBool, lp_on),
    FX_FIELD(kFxFloat, lp_freq),
    FX_FIELD(kFxInt, lp_cascade),
    FX_FIELD(kFxBool, peak_on),
    FX_FIELD(kFxFloat, peak_freq),
    FX_FIELD(kFxFloat, peak_q),
    FX_FIELD(kFxFloat, peak_gain_db),
    FX_FIELD(kFxBool, ring_on),
    FX_FIELD(kFxFloat, ring_freq),
    FX_FIELD(kFxFloat, ring_mix),
    FX_FIELD(kFxBool, comb_on),
    FX_FIELD(kFxFloat, comb_ms),
    FX_FIELD(kFxFloat, comb_feedback),
    FX_FIELD(kFxFloat, comb_mix),
    FX_FIELD(kFxFloat, comb_damp),
    FX_FIELD(kFxBool, clip_on),
    FX_FIELD(kFxInt, clip_factor),
    FX_FIELD(kFxBool, character_on),
    FX_FIELD(kFxBool, vc_on),
    FX_FIELD(kFxFloat, formant_scale),
    FX_FIELD(kFxFloat, formant_db),
    FX_FIELD(kFxFloat, formant_q),
    FX_FIELD(kFxFloat, nasal_db),
    FX_FIELD(kFxFloat, tilt_db),
    FX_FIELD(kFxBool, comp_on),
    FX_FIELD(kFxFloat, comp_threshold_db),
    FX_FIELD(kFxFloat, comp_ratio),
    FX_FIELD(kFxFloat, comp_attack_ms),
    FX_FIELD(kFxFloat, comp_release_ms),
    FX_FIELD(kFxFloat, comp_makeup_db),
    FX_FIELD(kFxFloat, char_mix),
    FX_FIELD(kFxBool, res_on),
    FX_FIELD(kFxBool, res1_on),
    FX_FIELD(kFxFloat, res1_freq),
    FX_FIELD(kFxFloat, res1_q),
    FX_FIELD(kFxFloat, res1_gain_db),
    FX_FIELD(kFxBool, res2_on),
    FX_FIELD(kFxFloat, res2_freq),
    FX_FIELD(kFxFloat, res2_q),
    FX_FIELD(kFxFloat, res2_gain_db),
    FX_FIELD(kFxBool, res3_on),
    FX_FIELD(kFxFloat, res3_freq),
    FX_FIELD(kFxFloat, res3_q),
    FX_FIELD(kFxFloat, res3_gain_db),
    FX_FIELD(kFxBool, res4_on),
    FX_FIELD(kFxFloat, res4_freq),
    FX_FIELD(kFxFloat, res4_q),
    FX_FIELD(kFxFloat, res4_gain_db),
    FX_FIELD(kFxBool, sat_on),
    FX_FIELD(kFxFloat, sat_drive_db),
    FX_FIELD(kFxFloat, sat_bias),
    FX_FIELD(kFxFloat, sat_mix),
    FX_FIELD(kFxFloat, sat_out_db),
    FX_FIELD(kFxBool, helmet_on),
    FX_FIELD(kFxFloat, helmet_low_hz),
    FX_FIELD(kFxFloat, helmet_high_hz),
    FX_FIELD(kFxFloat, helmet_reflect_ms),
    FX_FIELD(kFxFloat, helmet_reflect_fb),
    FX_FIELD(kFxFloat, helmet_reflect_mix),
    FX_FIELD(kFxFloat, helmet_am_hz),
    FX_FIELD(kFxFloat, helmet_am_depth),
    FX_FIELD(kFxBool, feq_on),
    FX_FIELD(kFxFloat, feq_low_hz),
    FX_FIELD(kFxFloat, feq_low_db),
    FX_FIELD(kFxFloat, feq_high_hz),
    FX_FIELD(kFxFloat, feq_high_db),
    FX_FIELD(kFxBool, lim_on),
    FX_FIELD(kFxFloat, lim_ceiling_db),
    FX_FIELD(kFxFloat, lim_release_ms),
    FX_FIELD(kFxBool, vt_on),
    FX_FIELD(kFxFloat, vt_formant),
    FX_FIELD(kFxFloat, vt_resonance),
    FX_FIELD(kFxFloat, vt_mix),
    FX_FIELD(kFxFloat, out_db),
};

/* To add a character voice (e.g. Chopper): add a row here with voice = true
   and a matching branch in fx_apply_preset(). */
const FxPresetInfo kPresets[] = {
    {"clean", "Clean / Bypass", true},
    {"r3x", "DJ R3X", true},
    {"r3x-vocal", "DJ R3X (vocal model)", true},
    {"tie", "TIE Pilot", true},
    {"trooper", "Stormtrooper", true},
    {"droid-voice", "Droid", true},
    {"dark-mech", "Dark Mechanical", false},
    {"quirky-droid", "Quirky Droid", false},
    {"stormtrooper", "Stormtrooper (classic)", false},
    {"tiepilot", "TIE Pilot (classic)", false},
    {"droid", "Droid (classic ring mod)", false},
    {"radio", "Radio", false},
    {"villain", "Villain (deep)", false},
};

const float kHelmetReflectMaxSec = 0.030f;

/* Helmet-cavity resonances (starting points). */
void set_helmet_resonances(FxParams* p) {
    p->res1_on = p->res2_on = p->res3_on = p->res4_on = true;
    p->res1_freq = 500.0f;
    p->res2_freq = 900.0f;
    p->res3_freq = 1800.0f;
    p->res4_freq = 3000.0f;
}

/* Droid-body resonances (starting points). */
void set_droid_resonances(FxParams* p) {
    p->res1_on = p->res2_on = p->res3_on = p->res4_on = true;
    p->res1_freq = 700.0f;
    p->res2_freq = 1200.0f;
    p->res3_freq = 2200.0f;
    p->res4_freq = 3500.0f;
}

float clampf(float v, float lo, float hi) {
    if (!(v == v)) {
        return lo;
    }
    return v < lo ? lo : (v > hi ? hi : v);
}

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void set_defaults(FxParams* p) {
    memset(p, 0, sizeof(*p));
    p->enabled = true;
    p->volume_db = 0.0f;
    p->pitch_semitones = 0.0f;
    p->hp_freq = 150.0f;
    p->hp_cascade = 1;
    p->lp_freq = 8000.0f;
    p->lp_cascade = 1;
    p->peak_freq = 2000.0f;
    p->peak_q = 1.0f;
    p->peak_gain_db = 6.0f;
    p->ring_freq = 30.0f;
    p->ring_mix = 1.0f;
    p->comb_ms = 3.0f;
    p->comb_feedback = 0.0f;
    p->comb_mix = 0.3f;
    p->comb_damp = 0.0f;
    p->clip_factor = 3;

    /* Character stages: off, but with usable values if switched on. */
    p->formant_scale = 1.0f;
    p->formant_db = 4.0f;
    p->formant_q = 3.0f;
    p->nasal_db = 0.0f;
    p->tilt_db = 0.0f;
    p->comp_threshold_db = -24.0f;
    p->comp_ratio = 3.0f;
    p->comp_attack_ms = 5.0f;
    p->comp_release_ms = 80.0f;
    p->comp_makeup_db = 6.0f;
    p->char_mix = 0.7f;
    set_helmet_resonances(p);
    p->res1_q = p->res2_q = p->res3_q = p->res4_q = 3.0f;
    p->res1_gain_db = p->res2_gain_db = p->res3_gain_db = p->res4_gain_db = 4.0f;
    p->sat_drive_db = 6.0f;
    p->sat_bias = 0.0f;
    p->sat_mix = 0.5f;
    p->sat_out_db = 0.0f;
    p->helmet_low_hz = 300.0f;
    p->helmet_high_hz = 3500.0f;
    p->helmet_reflect_ms = 1.2f;
    p->helmet_reflect_fb = 0.3f;
    p->helmet_reflect_mix = 0.3f;
    p->helmet_am_hz = 50.0f;
    p->helmet_am_depth = 0.0f;
    p->feq_low_hz = 200.0f;
    p->feq_low_db = 0.0f;
    p->feq_high_hz = 4000.0f;
    p->feq_high_db = 0.0f;
    p->lim_ceiling_db = -1.0f;
    p->lim_release_ms = 60.0f;
    p->vt_formant = 1.0f;
    p->vt_resonance = 0.0f;
    p->vt_mix = 1.0f;
}

}  // namespace

const FxField* fx_fields(int* count) {
    *count = (int)(sizeof(kFields) / sizeof(kFields[0]));
    return kFields;
}

const FxPresetInfo* fx_presets(int* count) {
    *count = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
    return kPresets;
}

bool fx_apply_preset(const char* id, FxParams* p) {
    FxParams n;
    set_defaults(&n);
    if (!strcmp(id, "clean")) {
        /* defaults: every effect off */
    } else if (!strcmp(id, "r3x")) {
        /* Bright, nasal cantina droid through a small speaker, with a metallic edge. */
        n.volume_db = 3.0f;
        n.pitch_on = true;
        n.pitch_semitones = 1.0f;
        n.hp_on = true;
        n.hp_freq = 350.0f;
        n.hp_cascade = 2;
        n.lp_on = true;
        n.lp_freq = 5000.0f;
        n.lp_cascade = 2;
        n.peak_on = true;
        n.peak_freq = 2500.0f;
        n.peak_q = 1.0f;
        n.peak_gain_db = 9.0f;
        n.ring_on = true;
        n.ring_freq = 160.0f;
        n.ring_mix = 0.3f;
        n.comb_on = true;
        n.comb_ms = 2.0f;
        n.comb_feedback = 0.0f;
        n.comb_mix = 0.3f;
        n.clip_on = true;
        n.clip_factor = 4;
    } else if (!strcmp(id, "r3x-vocal")) {
        /* R3X from a different vocal system: a smaller, brighter, more
           resonant tract (formants moved independently of pitch), a nasal
           resonance and forward presence. No ring mod, comb or clipping. */
        n.pitch_on = true;
        n.pitch_semitones = 1.0f;
        n.vt_on = true;
        n.vt_formant = 1.15f;
        n.vt_resonance = 0.4f;
        n.vt_mix = 0.9f;
        n.hp_on = true;
        n.hp_freq = 200.0f;
        n.hp_cascade = 2;
        n.lp_on = true;
        n.lp_freq = 7500.0f;
        n.peak_on = true;
        n.peak_freq = 2800.0f;
        n.peak_q = 1.2f;
        n.peak_gain_db = 4.0f;
        n.character_on = true;
        n.vc_on = true;
        n.formant_db = 0.0f;
        n.nasal_db = 5.0f;
        n.tilt_db = 2.0f;
        n.comp_on = true;
        n.comp_threshold_db = -26.0f;
        n.comp_ratio = 3.0f;
        n.comp_makeup_db = 6.0f;
        n.char_mix = 0.0f;
        n.lim_on = true;
    } else if (!strcmp(id, "droid")) {
        n.hp_on = true;
        n.hp_freq = 300.0f;
        n.lp_on = true;
        n.lp_freq = 4000.0f;
        n.ring_on = true;
        n.ring_freq = 30.0f;
        n.ring_mix = 1.0f;
        n.clip_on = true;
        n.clip_factor = 4;
    } else if (!strcmp(id, "stormtrooper")) {
        /* Helmet mic into a radio: boxy, band-limited, gritty. */
        n.volume_db = 4.0f;
        n.hp_on = true;
        n.hp_freq = 400.0f;
        n.hp_cascade = 3;
        n.lp_on = true;
        n.lp_freq = 3200.0f;
        n.lp_cascade = 3;
        n.peak_on = true;
        n.peak_freq = 1200.0f;
        n.peak_q = 1.5f;
        n.peak_gain_db = 6.0f;
        n.comb_on = true;
        n.comb_ms = 1.5f;
        n.comb_feedback = 0.0f;
        n.comb_mix = 0.3f;
        n.clip_on = true;
        n.clip_factor = 6;
    } else if (!strcmp(id, "tiepilot")) {
        /* Sealed flight mask over cockpit comms: narrower, lower, more distorted. */
        n.volume_db = 5.0f;
        n.pitch_on = true;
        n.pitch_semitones = -1.0f;
        n.hp_on = true;
        n.hp_freq = 500.0f;
        n.hp_cascade = 3;
        n.lp_on = true;
        n.lp_freq = 2500.0f;
        n.lp_cascade = 3;
        n.peak_on = true;
        n.peak_freq = 900.0f;
        n.peak_q = 2.0f;
        n.peak_gain_db = 8.0f;
        n.ring_on = true;
        n.ring_freq = 50.0f;
        n.ring_mix = 0.1f;
        n.comb_on = true;
        n.comb_ms = 2.5f;
        n.comb_feedback = 0.0f;
        n.comb_mix = 0.3f;
        n.clip_on = true;
        n.clip_factor = 7;
    } else if (!strcmp(id, "radio")) {
        n.hp_on = true;
        n.hp_freq = 400.0f;
        n.hp_cascade = 3;
        n.lp_on = true;
        n.lp_freq = 3000.0f;
        n.lp_cascade = 3;
        n.clip_on = true;
        n.clip_factor = 5;
    } else if (!strcmp(id, "villain")) {
        n.pitch_on = true;
        n.pitch_semitones = -5.0f;
        n.hp_on = true;
        n.hp_freq = 80.0f;
        n.lp_on = true;
        n.lp_freq = 5000.0f;
        n.comb_on = true;
        n.comb_ms = 45.0f;
        n.comb_feedback = 0.25f;
        n.comb_mix = 0.25f;
        n.clip_on = true;
        n.clip_factor = 2;
    } else if (!strcmp(id, "dark-mech")) {
        /* Deep, armoured and mechanical: lower pitch, larger-sounding vocal
           resonances, damped metal cavity, helmet resonances in parallel. */
        n.pitch_on = true;
        n.pitch_semitones = -4.0f;
        n.hp_on = true;
        n.hp_freq = 70.0f;
        n.comb_on = true;
        n.comb_ms = 4.0f;
        n.comb_feedback = 0.35f;
        n.comb_damp = 0.5f;
        n.comb_mix = 0.25f;
        n.character_on = true;
        n.vc_on = true;
        n.formant_scale = 0.82f;
        n.formant_db = 4.0f;
        n.formant_q = 2.5f;
        n.tilt_db = -3.0f;
        n.comp_on = true;
        n.comp_threshold_db = -26.0f;
        n.comp_ratio = 3.0f;
        n.comp_release_ms = 100.0f;
        n.char_mix = 0.6f;
        n.res_on = true;
        set_helmet_resonances(&n);
        n.res1_q = 2.5f;
        n.res1_gain_db = 3.0f;
        n.res2_gain_db = 4.0f;
        n.res3_gain_db = 3.0f;
        n.res4_q = 4.0f;
        n.res4_gain_db = 2.0f;
        n.sat_on = true;
        n.sat_drive_db = 9.0f;
        n.sat_bias = 0.1f;
        n.sat_mix = 0.4f;
        n.helmet_on = true;
        n.helmet_low_hz = 90.0f;
        n.helmet_high_hz = 6000.0f;
        n.helmet_reflect_ms = 0.8f;
        n.helmet_reflect_fb = 0.2f;
        n.helmet_reflect_mix = 0.2f;
        n.feq_on = true;
        n.feq_low_hz = 150.0f;
        n.feq_low_db = 2.0f;
        n.feq_high_hz = 5000.0f;
        n.feq_high_db = -2.0f;
        n.lim_on = true;
    } else if (!strcmp(id, "trooper")) {
        /* Helmet + comms: band-limited, compressed, gritty, with short
           plastic-shell reflections; some full-band dry keeps it intelligible. */
        n.pitch_on = true;
        n.pitch_semitones = -1.0f;
        n.character_on = true;
        n.comp_on = true;
        n.comp_threshold_db = -28.0f;
        n.comp_ratio = 4.0f;
        n.comp_attack_ms = 3.0f;
        n.comp_release_ms = 60.0f;
        n.comp_makeup_db = 8.0f;
        n.char_mix = 0.85f;
        n.res_on = true;
        set_helmet_resonances(&n);
        n.res1_q = 2.0f;
        n.res1_gain_db = 2.0f;
        n.res2_q = 2.5f;
        n.res2_gain_db = 4.0f;
        n.res3_gain_db = 5.0f;
        n.res4_gain_db = 3.0f;
        n.sat_on = true;
        n.sat_drive_db = 12.0f;
        n.sat_mix = 0.6f;
        n.helmet_on = true;
        n.helmet_low_hz = 300.0f;
        n.helmet_high_hz = 3800.0f;
        n.helmet_reflect_ms = 1.2f;
        n.helmet_reflect_fb = 0.35f;
        n.helmet_reflect_mix = 0.35f;
        n.lim_on = true;
    } else if (!strcmp(id, "tie")) {
        /* Sealed flight mask over cockpit comms: narrower, harder compression,
           asymmetric grit, slight engine-hum AM. */
        n.pitch_on = true;
        n.pitch_semitones = -1.5f;
        n.character_on = true;
        n.comp_on = true;
        n.comp_threshold_db = -30.0f;
        n.comp_ratio = 6.0f;
        n.comp_attack_ms = 2.0f;
        n.comp_release_ms = 50.0f;
        n.comp_makeup_db = 10.0f;
        n.char_mix = 0.9f;
        n.res_on = true;
        set_helmet_resonances(&n);
        n.res1_q = 2.0f;
        n.res1_gain_db = 3.0f;
        n.res2_gain_db = 6.0f;
        n.res3_gain_db = 4.0f;
        n.res4_q = 4.0f;
        n.res4_gain_db = 2.0f;
        n.sat_on = true;
        n.sat_drive_db = 15.0f;
        n.sat_bias = 0.15f;
        n.sat_mix = 0.7f;
        n.helmet_on = true;
        n.helmet_low_hz = 450.0f;
        n.helmet_high_hz = 2800.0f;
        n.helmet_reflect_ms = 0.8f;
        n.helmet_reflect_fb = 0.45f;
        n.helmet_reflect_mix = 0.35f;
        n.helmet_am_hz = 50.0f;
        n.helmet_am_depth = 0.08f;
        n.lim_on = true;
    } else if (!strcmp(id, "droid-voice")) {
        /* Metallic droid body: tuned resonances, ringing cavity, light ring mod. */
        n.pitch_on = true;
        n.pitch_semitones = 2.0f;
        n.ring_on = true;
        n.ring_freq = 110.0f;
        n.ring_mix = 0.25f;
        n.comb_on = true;
        n.comb_ms = 1.0f;
        n.comb_feedback = 0.5f;
        n.comb_damp = 0.2f;
        n.comb_mix = 0.3f;
        n.character_on = true;
        n.comp_on = true;
        n.comp_makeup_db = 5.0f;
        n.char_mix = 0.55f;
        n.res_on = true;
        set_droid_resonances(&n);
        n.res1_q = n.res2_q = n.res3_q = n.res4_q = 4.0f;
        n.res1_gain_db = n.res2_gain_db = n.res3_gain_db = n.res4_gain_db = 5.0f;
        n.sat_on = true;
        n.sat_drive_db = 6.0f;
        n.sat_mix = 0.3f;
        n.lim_on = true;
    } else if (!strcmp(id, "quirky-droid")) {
        /* Small, nasal, bouncy droid: raised resonances, nasal peak, tight
           compression, a touch of ring mod. */
        n.pitch_on = true;
        n.pitch_semitones = 2.0f;
        n.ring_on = true;
        n.ring_freq = 160.0f;
        n.ring_mix = 0.15f;
        n.character_on = true;
        n.vc_on = true;
        n.formant_scale = 1.2f;
        n.formant_db = 3.0f;
        n.nasal_db = 7.0f;
        n.tilt_db = 2.0f;
        n.comp_on = true;
        n.comp_threshold_db = -26.0f;
        n.comp_ratio = 5.0f;
        n.comp_attack_ms = 3.0f;
        n.comp_release_ms = 60.0f;
        n.comp_makeup_db = 7.0f;
        n.char_mix = 0.5f;
        n.res_on = true;
        set_droid_resonances(&n);
        n.res1_q = n.res2_q = n.res3_q = n.res4_q = 5.0f;
        n.sat_on = true;
        n.sat_drive_db = 6.0f;
        n.sat_mix = 0.3f;
        n.lim_on = true;
    } else {
        return false;
    }
    snprintf(n.preset, sizeof(n.preset), "%s", id);
    *p = n;
    return true;
}

void fx_clamp(FxParams* p) {
    p->preset[sizeof(p->preset) - 1] = '\0';
    p->volume_db = clampf(p->volume_db, -24.0f, 12.0f);
    p->pitch_semitones = clampf(p->pitch_semitones, -12.0f, 12.0f);
    p->hp_freq = clampf(p->hp_freq, 20.0f, 4000.0f);
    p->hp_cascade = clampi(p->hp_cascade, 1, 4);
    p->lp_freq = clampf(p->lp_freq, 500.0f, 20000.0f);
    p->lp_cascade = clampi(p->lp_cascade, 1, 4);
    p->peak_freq = clampf(p->peak_freq, 50.0f, 10000.0f);
    p->peak_q = clampf(p->peak_q, 0.2f, 10.0f);
    p->peak_gain_db = clampf(p->peak_gain_db, -18.0f, 18.0f);
    p->ring_freq = clampf(p->ring_freq, 1.0f, 2000.0f);
    p->ring_mix = clampf(p->ring_mix, 0.0f, 1.0f);
    p->comb_ms = clampf(p->comb_ms, 0.2f, kCombMaxSec * 1000.0f);
    p->comb_feedback = clampf(p->comb_feedback, 0.0f, 0.95f);
    p->comb_mix = clampf(p->comb_mix, 0.0f, 1.0f);
    p->comb_damp = clampf(p->comb_damp, 0.0f, 0.9f);
    p->clip_factor = clampi(p->clip_factor, 1, 10);

    p->formant_scale = clampf(p->formant_scale, 0.6f, 1.6f);
    p->formant_db = clampf(p->formant_db, -12.0f, 12.0f);
    p->formant_q = clampf(p->formant_q, 0.5f, 10.0f);
    p->nasal_db = clampf(p->nasal_db, -12.0f, 12.0f);
    p->tilt_db = clampf(p->tilt_db, -12.0f, 12.0f);
    p->comp_threshold_db = clampf(p->comp_threshold_db, -60.0f, 0.0f);
    p->comp_ratio = clampf(p->comp_ratio, 1.0f, 20.0f);
    p->comp_attack_ms = clampf(p->comp_attack_ms, 0.1f, 100.0f);
    p->comp_release_ms = clampf(p->comp_release_ms, 5.0f, 1000.0f);
    p->comp_makeup_db = clampf(p->comp_makeup_db, -12.0f, 24.0f);
    p->char_mix = clampf(p->char_mix, 0.0f, 1.0f);
    float* res_freq[] = {&p->res1_freq, &p->res2_freq, &p->res3_freq, &p->res4_freq};
    float* res_q[] = {&p->res1_q, &p->res2_q, &p->res3_q, &p->res4_q};
    float* res_gain[] = {&p->res1_gain_db, &p->res2_gain_db, &p->res3_gain_db, &p->res4_gain_db};
    for (int i = 0; i < 4; i++) {
        *res_freq[i] = clampf(*res_freq[i], 50.0f, 10000.0f);
        *res_q[i] = clampf(*res_q[i], 0.3f, 20.0f);
        *res_gain[i] = clampf(*res_gain[i], -18.0f, 18.0f);
    }
    p->sat_drive_db = clampf(p->sat_drive_db, 0.0f, 36.0f);
    p->sat_bias = clampf(p->sat_bias, -0.5f, 0.5f);
    p->sat_mix = clampf(p->sat_mix, 0.0f, 1.0f);
    p->sat_out_db = clampf(p->sat_out_db, -24.0f, 12.0f);
    p->helmet_low_hz = clampf(p->helmet_low_hz, 50.0f, 2000.0f);
    p->helmet_high_hz = clampf(p->helmet_high_hz, 1000.0f, 12000.0f);
    if (p->helmet_high_hz < p->helmet_low_hz * 1.5f) {
        p->helmet_high_hz = p->helmet_low_hz * 1.5f;
    }
    p->helmet_reflect_ms = clampf(p->helmet_reflect_ms, 0.1f, kHelmetReflectMaxSec * 1000.0f - 1.0f);
    p->helmet_reflect_fb = clampf(p->helmet_reflect_fb, 0.0f, 0.9f);
    p->helmet_reflect_mix = clampf(p->helmet_reflect_mix, 0.0f, 1.0f);
    p->helmet_am_hz = clampf(p->helmet_am_hz, 0.1f, 500.0f);
    p->helmet_am_depth = clampf(p->helmet_am_depth, 0.0f, 1.0f);
    p->feq_low_hz = clampf(p->feq_low_hz, 40.0f, 1000.0f);
    p->feq_low_db = clampf(p->feq_low_db, -12.0f, 12.0f);
    p->feq_high_hz = clampf(p->feq_high_hz, 1000.0f, 16000.0f);
    p->feq_high_db = clampf(p->feq_high_db, -12.0f, 12.0f);
    p->lim_ceiling_db = clampf(p->lim_ceiling_db, -12.0f, 0.0f);
    p->lim_release_ms = clampf(p->lim_release_ms, 5.0f, 1000.0f);
    p->vt_formant = clampf(p->vt_formant, 0.7f, 1.5f);
    p->vt_resonance = clampf(p->vt_resonance, -1.0f, 1.0f);
    p->vt_mix = clampf(p->vt_mix, 0.0f, 1.0f);
    p->out_db = clampf(p->out_db, -24.0f, 12.0f);
}

VoiceChain::VoiceChain(unsigned int rate, unsigned int channels)
    : rate_(rate), channels_(channels), ch_(channels) {
    for (Channel& c : ch_) {
        c.filters.reserve(kMaxFilters);
        c.pitch.buf.assign(kPitchBufLen, 0.0f);
        c.vt_pitch.buf.assign(kPitchBufLen, 0.0f);
        c.vt.init((float)rate);
        c.comb.init((size_t)(kCombMaxSec * rate) + 1);
        c.helmet.init((size_t)(kHelmetReflectMaxSec * rate) + 1);
    }
    FxParams clean;
    fx_apply_preset("clean", &clean);
    configure(clean);
}

void VoiceChain::configure(const FxParams& in, int mode) {
    FxParams e = in;
    fx_clamp(&e);
    if (mode == kFxModeBypass) {
        e.enabled = false;
    } else if (mode == kFxModeClassic) {
        e.character_on = false;
        e.lim_on = false;
        e.comb_damp = 0.0f;
        e.vt_on = false;
    }
    const bool voice_change =
        mode != mode_ || (strcmp(e.preset, p_.preset) != 0 && strcmp(e.preset, "custom") != 0);
    mode_ = mode;
    if (configured_ && voice_change) {
        pending_ = e;
        has_pending_ = true;
        return;
    }
    if (!configured_) {
        apply(e);
        target_ = e;
        configured_ = true;
        return;
    }
    target_ = e;
    gliding_ = true;
}

/* One glide step per audio period: floats move ~30% of the way to the
   target (about 20 ms to settle), switches and counts change at once. */
void VoiceChain::glide_step() {
    FxParams cur = target_;
    bool done = true;
    int count = 0;
    const FxField* fields = fx_fields(&count);
    for (int i = 0; i < count; i++) {
        if (fields[i].kind != kFxFloat) {
            continue;
        }
        const float from = *(const float*)((const char*)&p_ + fields[i].offset);
        float* to = (float*)((char*)&cur + fields[i].offset);
        const float d = *to - from;
        if (fabsf(d) > 1.0e-4f * (fabsf(*to) + 1.0f)) {
            *to = from + 0.3f * d;
            done = false;
        }
    }
    if (!done) {
        /* Stages switching off stay on until their mix/level has glided out. */
        for (int i = 0; i < count; i++) {
            if (fields[i].kind == kFxBool && *(const bool*)((const char*)&p_ + fields[i].offset)) {
                *(bool*)((char*)&cur + fields[i].offset) = true;
            }
        }
    }
    apply(cur);
    if (done) {
        gliding_ = false;
    }
}

void VoiceChain::apply(const FxParams& in) {
    p_ = in;

    FilterSpec specs[kMaxFilters];
    int n = 0;
    const float sr = (float)rate_;
    auto norm = [sr](float hz) { return fminf(hz / sr, 0.49f); };
    if (p_.hp_on) {
        for (int i = 0; i < p_.hp_cascade; i++) {
            specs[n++] = {bq_type_highpass, norm(p_.hp_freq), cascade_q(p_.hp_cascade, i), 0.0f};
        }
    }
    if (p_.lp_on) {
        for (int i = 0; i < p_.lp_cascade; i++) {
            specs[n++] = {bq_type_lowpass, norm(p_.lp_freq), cascade_q(p_.lp_cascade, i), 0.0f};
        }
    }
    if (p_.peak_on) {
        specs[n++] = {bq_type_peak, norm(p_.peak_freq), p_.peak_q, p_.peak_gain_db};
    }

    const ResonatorBank::Band bands[4] = {
        {p_.res1_on, p_.res1_freq, p_.res1_q, p_.res1_gain_db},
        {p_.res2_on, p_.res2_freq, p_.res2_q, p_.res2_gain_db},
        {p_.res3_on, p_.res3_freq, p_.res3_q, p_.res3_gain_db},
        {p_.res4_on, p_.res4_freq, p_.res4_q, p_.res4_gain_db},
    };
    const float ms = 0.001f * sr;

    for (Channel& c : ch_) {
        bool same_layout = c.filters.size() == (size_t)n;
        for (int i = 0; same_layout && i < n; i++) {
            same_layout = c.filters[i].getType() == specs[i].type;
        }
        if (same_layout) {
            /* Keep filter state so live slider moves don't click. */
            for (int i = 0; i < n; i++) {
                c.filters[i].setBiquad(specs[i].type, specs[i].fc, specs[i].q, specs[i].gain_db);
            }
        } else {
            c.filters.clear(); /* capacity reserved in the constructor: no allocation */
            for (int i = 0; i < n; i++) {
                c.filters.emplace_back(specs[i].type, specs[i].fc, specs[i].q, specs[i].gain_db);
            }
        }

        c.comb.set(p_.comb_ms * ms, p_.comb_feedback, p_.comb_damp, p_.comb_mix);
        c.vocal.set(p_.formant_scale, p_.formant_db, p_.formant_q, p_.nasal_db, p_.tilt_db, sr);
        c.comp.set(p_.comp_threshold_db, p_.comp_ratio, p_.comp_attack_ms, p_.comp_release_ms, p_.comp_makeup_db,
                   sr);
        c.res.set(bands, 4, sr);
        c.sat.set(p_.sat_drive_db, p_.sat_bias, p_.sat_mix, p_.sat_out_db);
        c.helmet.set(p_.helmet_low_hz, p_.helmet_high_hz, p_.helmet_reflect_ms * ms, p_.helmet_reflect_fb,
                     p_.helmet_reflect_mix, p_.helmet_am_depth, sr);
        c.feq.set(p_.feq_low_hz, p_.feq_low_db, p_.feq_high_hz, p_.feq_high_db, sr);
        c.lim.set(p_.lim_ceiling_db, p_.lim_release_ms, sr);
        c.vt.set(p_.vt_formant, p_.vt_resonance);
    }

    volume_ = powf(10.0f, p_.volume_db / 20.0f);
    ring_step_ = 2.0f * (float)M_PI * p_.ring_freq / sr;
    am_step_ = 2.0f * (float)M_PI * p_.helmet_am_hz / sr;
    const float ratio = powf(2.0f, p_.pitch_semitones / 12.0f);
    pitch_step_ = (1.0f - ratio) / (kPitchWindowSec * sr);
    clip_factor_ = 1.0f + p_.clip_factor / 6.0f;
    out_gain_ = powf(10.0f, p_.out_db / 20.0f);
}

/* Clear delay tails so the previous voice doesn't bleed into the new one.
   Filters/envelopes keep state; they settle within the fade-in. */
void VoiceChain::reset_voice_state() {
    for (Channel& c : ch_) {
        c.comb.reset();
        c.helmet.reset();
        c.vt.reset();
    }
}

/* Two crossfaded taps sweeping a short delay line (low-latency pitch shift). */
float VoiceChain::pitch_sample(PitchState& c, float x) {
    const unsigned int mask = kPitchBufLen - 1;
    const float window = kPitchWindowSec * (float)rate_;
    c.buf[c.w] = x;

    auto tap = [&](float delay) {
        float pos = (float)c.w - delay;
        if (pos < 0.0f) {
            pos += (float)kPitchBufLen;
        }
        const unsigned int i0 = (unsigned int)pos & mask;
        const unsigned int i1 = (i0 + 1) & mask;
        const float frac = pos - floorf(pos);
        return c.buf[i0] + (c.buf[i1] - c.buf[i0]) * frac;
    };

    const float ph1 = c.phase;
    float ph2 = ph1 + 0.5f;
    if (ph2 >= 1.0f) {
        ph2 -= 1.0f;
    }
    const float s = sinf((float)M_PI * ph1);
    const float g1 = s * s;
    const float y = tap(ph1 * window) * g1 + tap(ph2 * window) * (1.0f - g1);

    c.w = (c.w + 1) & mask;
    c.phase += pitch_step_;
    c.phase -= floorf(c.phase);
    return y;
}

/* Parallel character processing: dry and wet share the vocal character and
   compressor, then the wet path gets resonance, saturation and helmet. */
float VoiceChain::character_sample(Channel& c, float x, float am_sine) {
    if (p_.vc_on) {
        x = c.vocal.process(x);
    }
    if (p_.comp_on) {
        x = c.comp.process(x);
    }
    float wet = x;
    if (p_.res_on) {
        wet = c.res.process(wet);
    }
    if (p_.sat_on) {
        wet = c.sat.process(wet);
    }
    if (p_.helmet_on) {
        wet = c.helmet.process(wet, am_sine);
    }
    x += (wet - x) * p_.char_mix;
    if (p_.feq_on) {
        x = c.feq.process(x);
    }
    return x;
}

void VoiceChain::process(int16_t* interleaved, unsigned int frames) {
    if (frames == 0) {
        return;
    }
    if (gliding_ && !has_pending_) {
        glide_step();
    }
    const bool fx = p_.enabled;
    const bool character = fx && p_.character_on;
    const bool am_on = character && p_.helmet_on && p_.helmet_am_depth > 0.0f;

    /* Voice-change crossfade: fade out this block, fade in the next one. */
    float fade = 1.0f;
    float fade_step = 0.0f;
    const bool fading = has_pending_ || fading_in_;
    if (has_pending_) {
        fade_step = -1.0f / (float)frames;
    } else if (fading_in_) {
        fade = 0.0f;
        fade_step = 1.0f / (float)frames;
        fading_in_ = false;
    }

    for (unsigned int f = 0; f < frames; f++) {
        const float ring = sinf(ring_phase_);
        const float am = am_on ? sinf(am_phase_) : 0.0f;
        for (unsigned int k = 0; k < channels_; k++) {
            int16_t* sample = &interleaved[(size_t)f * channels_ + k];
            float x = (float)*sample / 32768.0f * input_gain_;

            if (fx) {
                Channel& c = ch_[k];
                for (Biquad& b : c.filters) {
                    x = b.process(x);
                }
                if (p_.ring_on) {
                    x = x * (1.0f - p_.ring_mix) + x * ring * p_.ring_mix;
                }
                if (p_.comb_on) {
                    /* Feedback 0 = single delayed copy (metallic tone, no tail);
                       higher feedback adds ringing, damping darkens it. */
                    x = c.comb.process(x);
                }
                x *= volume_;
                if (p_.vt_on) {
                    /* Pitch moves only the excitation; formants come from the
                       (reshaped) vocal tract, so pitch and formants are independent. */
                    float e = c.vt.analyze(x);
                    if (p_.pitch_on) {
                        c.vt_pitch.phase = c.pitch.phase;
                        e = pitch_sample(c.vt_pitch, e);
                    }
                    const float voiced = c.vt.synth(e);
                    const float classic = p_.pitch_on ? pitch_sample(c.pitch, x) : x;
                    x = classic + (voiced - classic) * p_.vt_mix;
                } else if (p_.pitch_on) {
                    x = pitch_sample(c.pitch, x);
                }
                if (character) {
                    x = character_sample(c, x, am);
                }
                if (p_.clip_on) {
                    const float cx = x * clip_factor_;
                    x = (cx / (1.0f + 0.28f * cx * cx)) / clip_factor_;
                }
                x *= out_gain_;
                if (p_.lim_on) {
                    x = c.lim.process(x);
                }
            }
            if (fading) {
                x *= fade;
            }

            float out = x * 32768.0f;
            if (out > 32767.0f) {
                out = 32767.0f;
            } else if (out < -32768.0f) {
                out = -32768.0f;
            }
            *sample = (int16_t)out;
        }
        if (fx && p_.ring_on) {
            ring_phase_ += ring_step_;
            if (ring_phase_ > 2.0f * (float)M_PI) {
                ring_phase_ -= 2.0f * (float)M_PI;
            }
        }
        if (am_on) {
            am_phase_ += am_step_;
            if (am_phase_ > 2.0f * (float)M_PI) {
                am_phase_ -= 2.0f * (float)M_PI;
            }
        }
        fade += fade_step;
    }

    if (has_pending_) {
        apply(pending_);
        target_ = pending_;
        gliding_ = false;
        reset_voice_state();
        has_pending_ = false;
        fading_in_ = true;
    }
}
