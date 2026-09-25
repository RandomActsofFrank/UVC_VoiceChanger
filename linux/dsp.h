#pragma once

#include "biquad.h"

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

    bool clip_on;
    int clip_factor;
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
};

const FxField* fx_fields(int* count);
const FxPresetInfo* fx_presets(int* count);
bool fx_apply_preset(const char* id, FxParams* p);
void fx_clamp(FxParams* p);

/*
 * Effect chain, same order as ESP/Filters.cpp applyFilters():
 * filters -> ring mod -> echo/comb -> volume -> pitch -> clipping.
 * Each channel is processed independently (stereo preserved).
 */
class VoiceChain {
public:
    VoiceChain(unsigned int rate, unsigned int channels);

    void set_input_gain(float gain) { input_gain_ = gain; }
    void configure(const FxParams& p);
    void process(int16_t* interleaved, unsigned int frames);

private:
    struct FilterSpec {
        int type;
        float fc;
        float q;
        float gain_db;
    };

    struct Channel {
        std::vector<Biquad> filters;
        std::vector<float> pitch_buf;
        unsigned int pitch_w = 0;
        float pitch_phase = 0.0f;
        std::vector<float> comb_buf;
        unsigned int comb_w = 0;
    };

    float pitch_sample(Channel& c, float x);

    unsigned int rate_;
    unsigned int channels_;
    std::vector<Channel> ch_;
    FxParams p_{};
    float input_gain_ = 1.0f;
    float volume_ = 1.0f;
    float ring_phase_ = 0.0f;
    float ring_step_ = 0.0f;
    float pitch_step_ = 0.0f;
    unsigned int comb_delay_ = 1;
    float clip_factor_ = 1.0f;
};
