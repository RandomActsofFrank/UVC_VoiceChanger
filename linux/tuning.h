#pragma once

/*
 * Personal tuning: a second parameter layer on top of a voice preset.
 *
 *   voice preset (character, FxParams)  ->  personal tuning (TuneParams)
 *                                        ->  effective FxParams -> VoiceChain
 *
 * The preset defines the character; the tuning adapts it to the performer
 * with broad controls that each move a group of DSP parameters. Every
 * tuning control is relative and 0 = the preset exactly as designed, so the
 * "Default" tuning of every voice is all zeros. Tunings never modify the
 * preset itself; they are saved per voice in tunings.ini.
 */

#include "dsp.h"

#include <string>
#include <vector>

struct TuneParams {
    float pitch;      /* semitones added to the voice's pitch (-6..6) */
    float character;  /* -1 larger/darker tract .. +1 smaller/brighter */
    float body;       /* -1 thinner .. +1 fuller low/mid weight */
    float presence;   /* -1 softer .. +1 more forward upper mids/highs */
    float mechanical; /* -1 less .. +1 more mechanical resonance/modulation */
    float helmet;     /* -1 less .. +1 more helmet/cavity */
    float saturation; /* -1 cleaner .. +1 more harmonic grit */
    float wet;        /* -1 more natural voice .. +1 more character */
    float output_db;  /* output level (-12..12 dB) */
};

const FxField* tune_fields(int* count);
void tune_defaults(TuneParams* t);
void tune_clamp(TuneParams* t);

/* Effective DSP parameters for a character preset plus a personal tuning.
 * Every field a tuning control can touch is always written (even at 0), so
 * moving a control from 0 fades a stage in from silence rather than
 * switching it on at some leftover level. */
FxParams fx_apply_tuning(const FxParams& character, const TuneParams& tune);

/* Saved personal tunings, per voice preset id (tunings.ini, next to presets.ini). */
class TuneStore {
public:
    explicit TuneStore(const std::string& path);

    std::vector<std::string> names(const std::string& preset) const;
    /* name "" = Default (all zeros). */
    bool get(const std::string& preset, const std::string& name, TuneParams* out) const;
    /* Tuning that loads when the voice is selected; "" = Default. */
    std::string active(const std::string& preset) const;
    bool set_active(const std::string& preset, const std::string& name, std::string* err);
    /* Save (overwrite or create) and make it active. Returns the stored name or "". */
    std::string save(const std::string& preset, const std::string& name, const TuneParams& t, std::string* err);
    bool remove(const std::string& preset, const std::string& name, std::string* err);

private:
    struct Entry {
        std::string preset;
        std::string name;
        TuneParams tune;
    };
    struct Active {
        std::string preset;
        std::string name;
    };

    void load();
    bool write(std::string* err) const;
    Entry* find(const std::string& preset, const std::string& name);
    const Entry* find(const std::string& preset, const std::string& name) const;

    std::string path_;
    std::vector<Entry> entries_;
    std::vector<Active> active_;
};

/* tunings.ini in the same folder as the presets file. */
std::string tunings_path_for(const std::string& presets_path);
