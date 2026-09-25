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

/* ---- VocalTract ---- */

namespace {

const float kPreEmphasis = 0.95f;

/* Levinson-Durbin. a[0..p] direct form (a[0] = 1), k[0..p-1] reflection
   coefficients, *norm_err = prediction error / r[0] = prod(1 - k^2). */
bool levinson(const double* r, int p, double* a, float* k, double* norm_err) {
    double tmp[VocalTract::kOrder + 1];
    a[0] = 1.0;
    for (int i = 1; i <= p; i++) {
        a[i] = 0.0;
    }
    double err = r[0];
    if (!(err > 0.0)) {
        return false;
    }
    for (int i = 1; i <= p; i++) {
        double acc = r[i];
        for (int j = 1; j < i; j++) {
            acc += a[j] * r[i - j];
        }
        const double ki = -acc / err;
        if (!(fabs(ki) < 0.9995)) {
            return false;
        }
        for (int j = 1; j < i; j++) {
            tmp[j] = a[j] + ki * a[i - j];
        }
        for (int j = 1; j < i; j++) {
            a[j] = tmp[j];
        }
        a[i] = ki;
        k[i - 1] = (float)ki;
        err *= 1.0 - ki * ki;
    }
    *norm_err = err / r[0];
    return true;
}

/* Direct form -> reflection coefficients; fails if the filter is unstable. */
bool step_down(const double* a_in, int p, float* k, double* norm_err) {
    double a[VocalTract::kOrder + 1];
    double tmp[VocalTract::kOrder + 1];
    for (int i = 0; i <= p; i++) {
        a[i] = a_in[i];
    }
    double prod = 1.0;
    for (int i = p; i >= 1; i--) {
        const double ki = a[i];
        if (!(fabs(ki) < 0.9995)) {
            return false;
        }
        const double d = 1.0 - ki * ki;
        for (int j = 1; j < i; j++) {
            tmp[j] = (a[j] - ki * a[i - j]) / d;
        }
        for (int j = 1; j < i; j++) {
            a[j] = tmp[j];
        }
        k[i - 1] = (float)ki;
        prod *= d;
    }
    *norm_err = prod;
    return true;
}

}  // namespace

void VocalTract::init(float rate) {
    hist_.assign(kFrame, 0.0f);
    win_.resize(kFrame);
    frame_.assign(kFrame, 0.0);
    warped_.assign(kFrame, 0.0);
    for (int n = 0; n < kFrame; n++) {
        win_[n] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (n + 0.5f) / kFrame);
    }
    /* Gaussian lag window (~30 Hz) keeps the estimated resonances well-behaved. */
    for (int i = 0; i <= kOrder; i++) {
        const double x = 2.0 * M_PI * 30.0 * i / rate;
        lag_[i] = exp(-0.5 * x * x);
    }
    reset();
}

void VocalTract::set(float formant_scale, float resonance) {
    lambda_ = (formant_scale - 1.0f) / (formant_scale + 1.0f);
    log_gamma_ = resonance * 0.003f; /* +-1 -> about +-46 Hz bandwidth at 48 kHz */
}

void VocalTract::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    hw_ = 0;
    hop_count_ = 0;
    for (int i = 0; i < kOrder; i++) {
        ka_[i] = ks_[i] = ab_[i] = sb_[i] = 0.0f;
    }
    pre_x1_ = de_y1_ = 0.0f;
    gain_ = gain_target_ = 1.0f;
}

void VocalTract::update() {
    for (int n = 0; n < kFrame; n++) {
        frame_[n] = (double)hist_[(hw_ + (size_t)n) % kFrame] * win_[n];
    }
    double r[kOrder + 1];
    for (int i = 0; i <= kOrder; i++) {
        double acc = 0.0;
        for (int n = i; n < kFrame; n++) {
            acc += frame_[n] * frame_[n - i];
        }
        r[i] = acc * lag_[i];
    }
    if (r[0] < 1.0e-9) {
        return; /* silence: keep the previous tract */
    }
    r[0] *= 1.0001; /* -40 dB noise floor for numerical safety */

    double ac[kOrder + 1];
    float kc[kOrder];
    double err_c;
    if (!levinson(r, kOrder, ac, kc, &err_c)) {
        return;
    }

    double aw[kOrder + 1];
    float kw[kOrder];
    double err_w = err_c;
    bool warped = false;
    if (lambda_ != 0.0f) {
        /* Warped autocorrelation: correlate the frame with itself passed
           through a chain of first-order all-pass sections. */
        double rw[kOrder + 1];
        rw[0] = r[0];
        const double lam = lambda_;
        for (int n = 0; n < kFrame; n++) {
            warped_[n] = frame_[n];
        }
        for (int i = 1; i <= kOrder; i++) {
            double x1 = 0.0;
            double y1 = 0.0;
            double acc = 0.0;
            for (int n = 0; n < kFrame; n++) {
                const double x = warped_[n];
                const double y = -lam * x + x1 + lam * y1;
                x1 = x;
                y1 = y;
                warped_[n] = y;
                acc += frame_[n] * y;
            }
            rw[i] = acc * lag_[i];
        }
        warped = levinson(rw, kOrder, aw, kw, &err_w);
    }
    if (!warped) {
        for (int i = 0; i <= kOrder; i++) {
            aw[i] = ac[i];
        }
        for (int i = 0; i < kOrder; i++) {
            kw[i] = kc[i];
        }
        err_w = err_c;
    }

    float ks[kOrder];
    double err_s = err_w;
    for (int i = 0; i < kOrder; i++) {
        ks[i] = kw[i];
    }
    if (log_gamma_ != 0.0f) {
        /* Bandwidth change: a[j] * gamma^j moves every pole's radius. */
        double lg = log_gamma_;
        for (int attempt = 0; attempt < 4; attempt++, lg *= 0.5) {
            double ag[kOrder + 1];
            for (int j = 0; j <= kOrder; j++) {
                ag[j] = aw[j] * exp(lg * j);
            }
            float kg[kOrder];
            double err_g;
            if (step_down(ag, kOrder, kg, &err_g)) {
                for (int i = 0; i < kOrder; i++) {
                    ks[i] = kg[i];
                }
                err_s = err_g;
                break;
            }
        }
    }

    for (int i = 0; i < kOrder; i++) {
        ka_[i] = kc[i];
        ks_[i] = ks[i];
    }
    /* Excitation power is r0*err_c; the new all-pole filter has power gain
       1/err_s, so this keeps the output level equal to the input level. */
    float g = (float)sqrt(err_s / err_c);
    gain_target_ = g < 0.1f ? 0.1f : (g > 4.0f ? 4.0f : g);
}

float VocalTract::analyze(float x) {
    const float pe = x - kPreEmphasis * pre_x1_;
    pre_x1_ = x;
    hist_[hw_] = pe;
    hw_ = (hw_ + 1 == (size_t)kFrame) ? 0 : hw_ + 1;
    if (++hop_count_ >= kHop) {
        hop_count_ = 0;
        update();
    }
    float f = pe;
    float b = pe;
    for (int m = 0; m < kOrder; m++) {
        const float bd = ab_[m];
        ab_[m] = b;
        const float fn = f + ka_[m] * bd;
        b = bd + ka_[m] * f;
        f = fn;
    }
    return f;
}

float VocalTract::synth(float e) {
    gain_ += (gain_target_ - gain_) * 0.005f;
    float f = e * gain_;
    for (int m = kOrder - 1; m >= 0; m--) {
        f -= ks_[m] * sb_[m];
        if (m + 1 < kOrder) {
            sb_[m + 1] = sb_[m] + ks_[m] * f;
        }
    }
    sb_[0] = f;
    const float y = f + kPreEmphasis * de_y1_;
    de_y1_ = y;
    return y;
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
