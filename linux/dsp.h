#pragma once

#include "biquad.h"
#include "character.h"

#include <stddef.h>
#include <stdint.h>

#include <vector>

/* Voice effect settings, adjustable live from the web page. */
struct FxParams {
    char preset[32]; /* preset id, or "custom" after manual changes */
    bool enabled;
    float volume_db;

    bool pitch_on;
    float pitch_semitones;

    bool hp_on;
    float hp_freq;
    int hp_cascade;

    bool lp_on;
    float lp_freq;
    int lp_cascade;

    bool peak_on;
    float peak_freq;
    float peak_q;
    float peak_gain_db;

    bool ring_on;
    float ring_freq;
    float ring_mix;

    bool comb_on;
    float comb_ms;
    float comb_feedback;
    float comb_mix;
    float comb_damp; /* 0 = classic comb; higher darkens the feedback tail */

    bool clip_on;
    int clip_factor;

    /* ---- Character stages (all off by default; classic presets unchanged) ---- */
    bool character_on;

    bool vc_on; /* vocal character (resonant-EQ formant approximation) */
    float formant_scale;
    float formant_db;
    float formant_q;
    float nasal_db;
    float tilt_db;

    bool comp_on;
    float comp_threshold_db;
    float comp_ratio;
    float comp_attack_ms;
    float comp_release_ms;
    float comp_makeup_db;

    float char_mix; /* 0 = dry only, 1 = wet (character) only */

    bool res_on;
    bool res1_on;
    float res1_freq;
    float res1_q;
    float res1_gain_db;
    bool res2_on;
    float res2_freq;
    float res2_q;
    float res2_gain_db;
    bool res3_on;
    float res3_freq;
    float res3_q;
    float res3_gain_db;
    bool res4_on;
    float res4_freq;
    float res4_q;
    float res4_gain_db;

    bool sat_on;
    float sat_drive_db;
    float sat_bias;
    float sat_mix;
    float sat_out_db;

    bool helmet_on;
    float helmet_low_hz;
    float helmet_high_hz;
    float helmet_reflect_ms;
    float helmet_reflect_fb;
    float helmet_reflect_mix;
    float helmet_am_hz;
    float helmet_am_depth;

    bool feq_on;
    float feq_low_hz;
    float feq_low_db;
    float feq_high_hz;
    float feq_high_db;

    bool lim_on;
    float lim_ceiling_db;
    float lim_release_ms;

    /* Vocal-tract model: pitch-independent formant shift + resonance. */
    bool vt_on;
    float vt_formant;   /* formant scale: >1 smaller/brighter tract, <1 larger */
    float vt_resonance; /* -1 softer .. +1 sharper, more resonant formants */
    float vt_mix;       /* 0 = classic pitch path only, 1 = vocal model only */
};

/* A/B listening mode (not saved in presets). */
enum FxMode {
    kFxModeBypass = 0,    /* input gain only */
    kFxModeClassic = 1,   /* original chain; all character stages forced off */
    kFxModeCharacter = 2, /* everything as configured */
};

enum FxFieldKind { kFxBool, kFxInt, kFxFloat };

struct FxField {
    const char* key;
    FxFieldKind kind;
    size_t offset;
};

struct FxPresetInfo {
    const char* id;
    const char* name;
    bool voice; /* shown as a main voice button; others are under "More voices" */
};

const FxField* fx_fields(int* count);
const FxPresetInfo* fx_presets(int* count);
bool fx_apply_preset(const char* id, FxParams* p);
void fx_clamp(FxParams* p);

/*
 * Signal chain per sample (classic part = ESP/Filters.cpp applyFilters order):
 *
 *   input gain
 *   -> HP x n -> LP x n -> presence peak          (classic)
 *   -> ring mod                                   (classic)
 *   -> comb / cavity (feedback damping optional)  (classic, improved)
 *   -> volume
 *   -> pitch shift                                (classic)
 *      [vt_on] in parallel: vocal-tract analysis -> pitch shift of the
 *      excitation only -> resynthesis with the reshaped tract; blended
 *      with the classic pitch path by vt_mix
 *   -> [character_on]
 *        vocal character -> compressor -> split:
 *          dry ------------------------------------------+
 *          wet: resonators -> saturator -> helmet/comms --+-> char_mix
 *        -> final shelf EQ
 *   -> soft clip                                  (classic)
 *   -> soft limiter                               (lim_on)
 *   -> int16
 *
 * Stereo: each channel has its own filters, delay lines and envelopes.
 * Shared across channels: the ring-mod and helmet-AM oscillator phases,
 * so both channels modulate in step and the stereo image stays stable.
 *
 * Real time: all buffers are allocated in the constructor; configure()
 * and process() never allocate.
 *
 * Voice changes (a different preset id, or a different A/B mode) are
 * crossfaded: the next block fades out on the old voice, the new
 * parameters are applied, the delay tails are cleared, and the block
 * after that fades in. Slider edits ("custom") apply immediately and
 * keep all state.
 */
class VoiceChain {
public:
    VoiceChain(unsigned int rate, unsigned int channels);

    void set_input_gain(float gain) { input_gain_ = gain; }
    void configure(const FxParams& p, int mode = kFxModeCharacter);
    void process(int16_t* interleaved, unsigned int frames);

private:
    struct FilterSpec {
        int type;
        float fc;
        float q;
        float gain_db;
    };
    static const int kMaxFilters = 9; /* 4 HP + 4 LP + peak */

    struct PitchState {
        std::vector<float> buf;
        unsigned int w = 0;
        float phase = 0.0f;
    };

    struct Channel {
        std::vector<Biquad> filters;
        PitchState pitch;
        PitchState vt_pitch; /* same settings as pitch, so both stay in step */
        VocalTract vt;
        CombDelay comb;
        VocalCharacter vocal;
        Compressor comp;
        ResonatorBank res;
        Saturator sat;
        HelmetProcessor helmet;
        ShelfEq feq;
        SoftLimiter lim;
    };

    void apply(const FxParams& p);
    void reset_voice_state();
    float pitch_sample(PitchState& s, float x);
    float character_sample(Channel& c, float x, float am_sine);

    unsigned int rate_;
    unsigned int channels_;
    std::vector<Channel> ch_;
    FxParams p_{};
    FxParams pending_{};
    bool has_pending_ = false;
    bool fading_in_ = false;
    bool configured_ = false;
    int mode_ = kFxModeCharacter;
    float input_gain_ = 1.0f;
    float volume_ = 1.0f;
    float ring_phase_ = 0.0f;
    float ring_step_ = 0.0f;
    float am_phase_ = 0.0f;
    float am_step_ = 0.0f;
    float pitch_step_ = 0.0f;
    float clip_factor_ = 1.0f;
};
