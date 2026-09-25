#pragma once

/*
 * Character-voice DSP stages used by VoiceChain (dsp.cpp).
 *
 * Real-time rules for everything here:
 *  - memory is allocated only in init() (called from VoiceChain's constructor),
 *    never in set*() or process();
 *  - process() is per-sample, per-channel, no locks, no I/O.
 * Parameter setters recompute coefficients and keep filter/envelope state,
 * so live slider moves don't reset the sound.
 */

#include "biquad.h"

#include <stddef.h>

#include <vector>

/* Butterworth Q for stage i of an n-stage cascade (same as ESP calcQvals). */
float cascade_q(int n, int i);

/* Delay line with feedback, damping and fractional delay.
 * damp = 0 and feedback = 0 reproduce the classic single-tap comb exactly
 * (apart from sub-sample delay precision). */
class CombDelay {
public:
    void init(size_t max_samples);
    void set(float delay_samples, float feedback, float damp, float mix);
    void reset(); /* clear the tail; no allocation */
    float process(float x);

private:
    std::vector<float> buf_;
    size_t w_ = 0;
    float delay_ = 1.0f;
    float feedback_ = 0.0f;
    float damp_ = 0.0f;
    float mix_ = 0.0f;
    float lp_ = 0.0f;
};

/* Feed-forward peak compressor with soft knee, dB-domain gain computer. */
class Compressor {
public:
    void set(float threshold_db, float ratio, float attack_ms, float release_ms, float makeup_db, float rate);
    float process(float x);

private:
    float threshold_db_ = 0.0f;
    float slope_ = 0.0f; /* 1/ratio - 1 */
    float attack_ = 0.0f;
    float release_ = 0.0f;
    float makeup_db_ = 0.0f;
    float env_ = 0.0f;
};

/* Zero-latency soft-knee peak limiter (instant attack, smooth release).
 * Output never exceeds the ceiling. */
class SoftLimiter {
public:
    void set(float ceiling_db, float release_ms, float rate);
    float process(float x);

private:
    float ceiling_ = 1.0f;
    float knee_ = 0.6f;
    float release_ = 0.0f;
    float env_ = 0.0f;
};

/* tanh waveshaper with unity small-signal gain, optional bias for even
 * harmonics, DC blocker, wet/dry mix and output gain. */
class Saturator {
public:
    void set(float drive_db, float bias, float mix, float output_db);
    float process(float x);

private:
    float drive_ = 1.0f;
    float bias_ = 0.0f;
    float bias_out_ = 0.0f;
    float inv_slope_ = 1.0f;
    float mix_ = 0.0f;
    float out_ = 1.0f;
    float dc_x_ = 0.0f;
    float dc_y_ = 0.0f;
};

/* Up to kMax independent peak resonances in series. */
class ResonatorBank {
public:
    static const int kMax = 4;
    struct Band {
        bool on;
        float freq;
        float q;
        float gain_db;
    };

    void set(const Band* bands, int count, float rate);
    float process(float x);

private:
    Biquad filt_[kMax];
    bool on_[kMax] = {};
};

/*
 * Formant-oriented vocal character (resonant-EQ approximation, NOT a true
 * formant shifter): imposes three vocal-tract-like resonances plus a nasal
 * resonance whose frequencies all scale with formant_scale, and a spectral
 * tilt. It colours the voice as larger/smaller/nasal; it does not move the
 * speaker's own formants.
 */
class VocalCharacter {
public:
    void set(float formant_scale, float formant_db, float formant_q, float nasal_db, float tilt_db, float rate);
    float process(float x);

private:
    static const int kBands = 6;
    Biquad filt_[kBands];
};

/*
 * Helmet / comms coloration: band limit (2x HP, 2x LP) -> very short damped
 * reflection -> optional amplitude modulation (shared oscillator passed in).
 * Resonant EQ, saturation and compression come from the neighbouring stages.
 */
class HelmetProcessor {
public:
    void init(size_t max_reflect_samples);
    void set(float low_hz, float high_hz, float reflect_samples, float reflect_fb, float reflect_mix,
             float am_depth, float rate);
    void reset() { reflect_.reset(); }
    float process(float x, float am_sine);

private:
    Biquad hp_[2];
    Biquad lp_[2];
    CombDelay reflect_;
    float am_depth_ = 0.0f;
};

/* Final low/high shelf EQ. */
class ShelfEq {
public:
    void set(float low_hz, float low_db, float high_hz, float high_db, float rate);
    float process(float x);

private:
    Biquad low_;
    Biquad high_;
};

/*
 * Vocal-tract model (LPC source/filter), used for pitch-independent formant
 * treatment:
 *   analyze(): pre-emphasis -> lattice inverse filter with the speaker's
 *              current vocal-tract estimate -> excitation (buzz/noise)
 *   (the excitation may be pitch-shifted in between)
 *   synth():   lattice all-pole filter with a reshaped vocal tract ->
 *              de-emphasis
 * The tract is re-estimated every kHop samples from the last kFrame input
 * samples, so the resonances follow the speaker's vowels. Reshaping:
 *  - formant_scale: frequency-warped LPC (all-pass warping), moves every
 *    formant up (>1, smaller head) or down (<1, larger head);
 *  - resonance: narrows (>0, more resonant/hollow) or widens (<0) formant
 *    bandwidths.
 * With formant_scale 1, resonance 0 and no pitch shift in between,
 * synth(analyze(x)) == x exactly.
 */
class VocalTract {
public:
    static const int kOrder = 24;
    static const int kFrame = 1024;
    static const int kHop = 128;

    void init(float rate);
    void set(float formant_scale, float resonance);
    void reset();
    float analyze(float x);
    float synth(float e);

private:
    void update();

    std::vector<float> hist_; /* pre-emphasised input, ring of kFrame */
    std::vector<float> win_;
    std::vector<double> frame_;
    std::vector<double> warped_;
    double lag_[kOrder + 1] = {};
    size_t hw_ = 0;
    int hop_count_ = 0;
    float ka_[kOrder] = {}; /* analysis reflection coefficients */
    float ks_[kOrder] = {}; /* synthesis reflection coefficients */
    float ab_[kOrder] = {}; /* analysis lattice state */
    float sb_[kOrder] = {}; /* synthesis lattice state */
    float pre_x1_ = 0.0f;
    float de_y1_ = 0.0f;
    float lambda_ = 0.0f;
    float log_gamma_ = 0.0f;
    float gain_ = 1.0f;
    float gain_target_ = 1.0f;
};

/* Flush denormals to zero on the calling thread (call from the audio thread). */
void dsp_enable_flush_to_zero();
