/*
 * Character-voice DSP stages (see character.h for the real-time rules).
 */

#include "character.h"

#include <math.h>
#include <stdint.h>

#include <algorithm>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace {

float db_to_lin(float db) {
    return powf(10.0f, db / 20.0f);
}

float norm_fc(float hz, float rate) {
    float fc = hz / rate;
    if (fc < 1.0e-4f) {
        fc = 1.0e-4f;
    }
    return fc > 0.49f ? 0.49f : fc;
}

float time_coef(float ms, float rate) {
    if (ms <= 0.0f) {
        return 0.0f;
    }
    return expf(-1.0f / (ms * 0.001f * rate));
}

}  // namespace

float cascade_q(int n, int i) {
    if (n <= 1) {
        return 0.707f;
    }
    return 1.0f / (2.0f * cosf((1.0f + 2.0f * i) * (float)M_PI / (4.0f * n)));
}

/* ---- CombDelay ---- */

void CombDelay::init(size_t max_samples) {
    buf_.assign(max_samples + 2, 0.0f);
    w_ = 0;
    lp_ = 0.0f;
}

void CombDelay::set(float delay_samples, float feedback, float damp, float mix) {
    const float max_delay = (float)buf_.size() - 2.0f;
    delay_ = delay_samples < 1.0f ? 1.0f : (delay_samples > max_delay ? max_delay : delay_samples);
    /* Loop gain = feedback * (damping low-pass gain <= 1) < 1: can't run away. */
    feedback_ = feedback < 0.0f ? 0.0f : (feedback > 0.95f ? 0.95f : feedback);
    damp_ = damp < 0.0f ? 0.0f : (damp > 0.95f ? 0.95f : damp);
    mix_ = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
}

void CombDelay::reset() {
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    lp_ = 0.0f;
}

float CombDelay::process(float x) {
    const size_t len = buf_.size();
    float pos = (float)w_ - delay_;
    if (pos < 0.0f) {
        pos += (float)len;
    }
    size_t i0 = (size_t)pos;
    if (i0 >= len) {
        i0 -= len;
    }
    const size_t i1 = (i0 + 1 == len) ? 0 : i0 + 1;
    const float frac = pos - floorf(pos);
    const float delayed = buf_[i0] + (buf_[i1] - buf_[i0]) * frac;

    lp_ = delayed + damp_ * (lp_ - delayed);
    buf_[w_] = x + feedback_ * lp_;
    w_ = (w_ + 1 == len) ? 0 : w_ + 1;
    return x * (1.0f - mix_) + delayed * mix_;
}

/* ---- Compressor ---- */

namespace {
const float kCompKneeDb = 6.0f;
}

void Compressor::set(float threshold_db, float ratio, float attack_ms, float release_ms, float makeup_db,
                     float rate) {
    threshold_db_ = threshold_db;
    slope_ = 1.0f / (ratio < 1.0f ? 1.0f : ratio) - 1.0f;
    attack_ = time_coef(attack_ms, rate);
    release_ = time_coef(release_ms, rate);
    makeup_db_ = makeup_db;
}

float Compressor::process(float x) {
    const float a = fabsf(x);
    const float coef = a > env_ ? attack_ : release_;
    env_ = a + coef * (env_ - a);

    const float env_db = 20.0f * log10f(env_ + 1.0e-9f);
    const float over = env_db - threshold_db_;
    float gr_db = 0.0f;
    if (2.0f * over >= kCompKneeDb) {
        gr_db = slope_ * over;
    } else if (2.0f * over > -kCompKneeDb) {
        const float t = over + kCompKneeDb * 0.5f;
        gr_db = slope_ * t * t / (2.0f * kCompKneeDb);
    }
    return x * db_to_lin(gr_db + makeup_db_);
}

/* ---- SoftLimiter ---- */

void SoftLimiter::set(float ceiling_db, float release_ms, float rate) {
    ceiling_ = db_to_lin(ceiling_db);
    knee_ = 0.6f * ceiling_;
    release_ = time_coef(release_ms, rate);
}

float SoftLimiter::process(float x) {
    const float a = fabsf(x);
    if (a > env_) {
        env_ = a; /* instant attack: |x| <= env, so |out| < ceiling */
    } else {
        env_ = a + release_ * (env_ - a);
    }
    if (env_ <= knee_) {
        return x;
    }
    const float span = ceiling_ - knee_;
    const float target = knee_ + span * tanhf((env_ - knee_) / span);
    return x * (target / env_);
}

/* ---- Saturator ---- */

void Saturator::set(float drive_db, float bias, float mix, float output_db) {
    drive_ = db_to_lin(drive_db);
    bias_ = bias;
    const float tb = tanhf(drive_ * bias_);
    bias_out_ = tb;
    inv_slope_ = 1.0f / (drive_ * (1.0f - tb * tb));
    mix_ = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
    out_ = db_to_lin(output_db);
}

float Saturator::process(float x) {
    /* Unity gain for small signals; peaks round off, bias adds even harmonics. */
    const float shaped = (tanhf(drive_ * (x + bias_)) - bias_out_) * inv_slope_;
    dc_y_ = shaped - dc_x_ + 0.9995f * dc_y_;
    dc_x_ = shaped;
    return (x * (1.0f - mix_) + dc_y_ * mix_) * out_;
}

/* ---- ResonatorBank ---- */

void ResonatorBank::set(const Band* bands, int count, float rate) {
    for (int i = 0; i < kMax; i++) {
        on_[i] = i < count && bands[i].on;
        if (on_[i]) {
            filt_[i].setBiquad(bq_type_peak, norm_fc(bands[i].freq, rate), bands[i].q, bands[i].gain_db);
        }
    }
}

float ResonatorBank::process(float x) {
    for (int i = 0; i < kMax; i++) {
        if (on_[i]) {
            x = filt_[i].process(x);
        }
    }
    return x;
}

/* ---- VocalCharacter ---- */

void VocalCharacter::set(float formant_scale, float formant_db, float formant_q, float nasal_db, float tilt_db,
                         float rate) {
    const float s = formant_scale;
    filt_[0].setBiquad(bq_type_peak, norm_fc(500.0f * s, rate), formant_q, formant_db);
    filt_[1].setBiquad(bq_type_peak, norm_fc(1500.0f * s, rate), formant_q, formant_db);
    filt_[2].setBiquad(bq_type_peak, norm_fc(2500.0f * s, rate), formant_q, formant_db);
    filt_[3].setBiquad(bq_type_peak, norm_fc(1100.0f * s, rate), 2.5f, nasal_db);
    filt_[4].setBiquad(bq_type_lowshelf, norm_fc(250.0f, rate), 0.707f, -0.5f * tilt_db);
    filt_[5].setBiquad(bq_type_highshelf, norm_fc(3000.0f, rate), 0.707f, 0.5f * tilt_db);
}

float VocalCharacter::process(float x) {
    for (int i = 0; i < kBands; i++) {
        x = filt_[i].process(x);
    }
    return x;
}

/* ---- HelmetProcessor ---- */

void HelmetProcessor::init(size_t max_reflect_samples) {
    reflect_.init(max_reflect_samples);
}

void HelmetProcessor::set(float low_hz, float high_hz, float reflect_samples, float reflect_fb, float reflect_mix,
                          float am_depth, float rate) {
    for (int i = 0; i < 2; i++) {
        hp_[i].setBiquad(bq_type_highpass, norm_fc(low_hz, rate), cascade_q(2, i), 0.0f);
        lp_[i].setBiquad(bq_type_lowpass, norm_fc(high_hz, rate), cascade_q(2, i), 0.0f);
    }
    reflect_.set(reflect_samples, reflect_fb, 0.3f, reflect_mix);
    am_depth_ = am_depth;
}

float HelmetProcessor::process(float x, float am_sine) {
    x = hp_[1].process(hp_[0].process(x));
    x = lp_[1].process(lp_[0].process(x));
    x = reflect_.process(x);
    if (am_depth_ > 0.0f) {
        x *= 1.0f - am_depth_ * 0.5f * (1.0f + am_sine);
    }
    return x;
}

/* ---- ShelfEq ---- */

void ShelfEq::set(float low_hz, float low_db, float high_hz, float high_db, float rate) {
    low_.setBiquad(bq_type_lowshelf, norm_fc(low_hz, rate), 0.707f, low_db);
    high_.setBiquad(bq_type_highshelf, norm_fc(high_hz, rate), 0.707f, high_db);
}

float ShelfEq::process(float x) {
    return high_.process(low_.process(x));
}

void dsp_enable_flush_to_zero() {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24);
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
#elif defined(__SSE__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
}
