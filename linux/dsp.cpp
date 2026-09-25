/*
 * Voice effects for the Linux engine.
 * Ported concepts from ESP/Filters.cpp (biquads, ring mod, reverb comb,
 * volume, pitch, soft clip), running per channel in float.
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
    FX_FIELD(kFxBool, clip_on),
    FX_FIELD(kFxInt, clip_factor),
};

const FxPresetInfo kPresets[] = {
    {"clean", "Clean (no effects)"},
    {"r3x", "DJ R3X"},
    {"droid", "Droid (ring mod)"},
    {"stormtrooper", "Stormtrooper"},
    {"tiepilot", "TIE Pilot"},
    {"radio", "Radio"},
    {"villain", "Villain (deep)"},
};

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
    p->comb_ms = 5.0f;
    p->comb_feedback = 0.5f;
    p->comb_mix = 0.5f;
    p->clip_factor = 3;
}

/* Butterworth Q for stage i of an n-stage cascade (ESP calcQvals). */
float cascade_q(int n, int i) {
    if (n <= 1) {
        return 0.707f;
    }
    return 1.0f / (2.0f * cosf((1.0f + 2.0f * i) * (float)M_PI / (4.0f * n)));
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
        n.comb_ms = 3.0f;
        n.comb_feedback = 0.25f;
        n.comb_mix = 0.35f;
        n.clip_on = true;
        n.clip_factor = 4;
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
        n.comb_feedback = 0.3f;
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
        n.comb_feedback = 0.35f;
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
    p->comb_ms = clampf(p->comb_ms, 1.0f, kCombMaxSec * 1000.0f);
    p->comb_feedback = clampf(p->comb_feedback, 0.0f, 0.95f);
    p->comb_mix = clampf(p->comb_mix, 0.0f, 1.0f);
    p->clip_factor = clampi(p->clip_factor, 1, 10);
}

VoiceChain::VoiceChain(unsigned int rate, unsigned int channels)
    : rate_(rate), channels_(channels), ch_(channels) {
    const size_t comb_len = (size_t)(kCombMaxSec * rate) + 1;
    for (Channel& c : ch_) {
        c.pitch_buf.assign(kPitchBufLen, 0.0f);
        c.comb_buf.assign(comb_len, 0.0f);
    }
    FxParams clean;
    fx_apply_preset("clean", &clean);
    configure(clean);
}

void VoiceChain::configure(const FxParams& in) {
    p_ = in;
    fx_clamp(&p_);

    std::vector<FilterSpec> specs;
    const float sr = (float)rate_;
    auto norm = [sr](float hz) { return fminf(hz / sr, 0.49f); };
    if (p_.hp_on) {
        for (int i = 0; i < p_.hp_cascade; i++) {
            specs.push_back({bq_type_highpass, norm(p_.hp_freq), cascade_q(p_.hp_cascade, i), 0.0f});
        }
    }
    if (p_.lp_on) {
        for (int i = 0; i < p_.lp_cascade; i++) {
            specs.push_back({bq_type_lowpass, norm(p_.lp_freq), cascade_q(p_.lp_cascade, i), 0.0f});
        }
    }
    if (p_.peak_on) {
        specs.push_back({bq_type_peak, norm(p_.peak_freq), p_.peak_q, p_.peak_gain_db});
    }

    for (Channel& c : ch_) {
        bool same_layout = c.filters.size() == specs.size();
        for (size_t i = 0; same_layout && i < specs.size(); i++) {
            same_layout = c.filters[i].getType() == specs[i].type;
        }
        if (same_layout) {
            /* Keep filter state so live slider moves don't click. */
            for (size_t i = 0; i < specs.size(); i++) {
                c.filters[i].setBiquad(specs[i].type, specs[i].fc, specs[i].q, specs[i].gain_db);
            }
        } else {
            c.filters.clear();
            for (const FilterSpec& s : specs) {
                c.filters.emplace_back(s.type, s.fc, s.q, s.gain_db);
            }
        }
    }

    volume_ = powf(10.0f, p_.volume_db / 20.0f);
    ring_step_ = 2.0f * (float)M_PI * p_.ring_freq / sr;
    const float ratio = powf(2.0f, p_.pitch_semitones / 12.0f);
    pitch_step_ = (1.0f - ratio) / (kPitchWindowSec * sr);
    comb_delay_ = (unsigned int)(p_.comb_ms * 0.001f * sr);
    if (comb_delay_ < 1) {
        comb_delay_ = 1;
    }
    clip_factor_ = 1.0f + p_.clip_factor / 6.0f;
}

/* Two crossfaded taps sweeping a short delay line (low-latency pitch shift). */
float VoiceChain::pitch_sample(Channel& c, float x) {
    const unsigned int mask = kPitchBufLen - 1;
    const float window = kPitchWindowSec * (float)rate_;
    c.pitch_buf[c.pitch_w] = x;

    auto tap = [&](float delay) {
        float pos = (float)c.pitch_w - delay;
        if (pos < 0.0f) {
            pos += (float)kPitchBufLen;
        }
        const unsigned int i0 = (unsigned int)pos & mask;
        const unsigned int i1 = (i0 + 1) & mask;
        const float frac = pos - floorf(pos);
        return c.pitch_buf[i0] + (c.pitch_buf[i1] - c.pitch_buf[i0]) * frac;
    };

    const float ph1 = c.pitch_phase;
    float ph2 = ph1 + 0.5f;
    if (ph2 >= 1.0f) {
        ph2 -= 1.0f;
    }
    const float s = sinf((float)M_PI * ph1);
    const float g1 = s * s;
    const float y = tap(ph1 * window) * g1 + tap(ph2 * window) * (1.0f - g1);

    c.pitch_w = (c.pitch_w + 1) & mask;
    c.pitch_phase += pitch_step_;
    c.pitch_phase -= floorf(c.pitch_phase);
    return y;
}

void VoiceChain::process(int16_t* interleaved, unsigned int frames) {
    const bool fx = p_.enabled;
    const size_t comb_len = ch_.empty() ? 1 : ch_[0].comb_buf.size();

    for (unsigned int f = 0; f < frames; f++) {
        const float ring = sinf(ring_phase_);
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
                    const size_t read = (c.comb_w + comb_len - comb_delay_) % comb_len;
                    const float y = x + p_.comb_feedback * c.comb_buf[read];
                    c.comb_buf[c.comb_w] = y;
                    c.comb_w = (unsigned int)((c.comb_w + 1) % comb_len);
                    x = x * (1.0f - p_.comb_mix) + y * p_.comb_mix;
                }
                x *= volume_;
                if (p_.pitch_on) {
                    x = pitch_sample(c, x);
                }
                if (p_.clip_on) {
                    const float cx = x * clip_factor_;
                    x = (cx / (1.0f + 0.28f * cx * cx)) / clip_factor_;
                }
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
    }
}
