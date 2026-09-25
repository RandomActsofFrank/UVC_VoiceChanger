#include "tuning.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {

#define TUNE_FIELD(name) {#name, kFxFloat, offsetof(TuneParams, name)}

const FxField kTuneFields[] = {
    TUNE_FIELD(pitch),
    TUNE_FIELD(character),
    TUNE_FIELD(body),
    TUNE_FIELD(presence),
    TUNE_FIELD(mechanical),
    TUNE_FIELD(helmet),
    TUNE_FIELD(saturation),
    TUNE_FIELD(wet),
    TUNE_FIELD(output_db),
};

const size_t kMaxTuneName = 24;

float clampf(float v, float lo, float hi) {
    if (!(v == v)) {
        return 0.0f;
    }
    return v < lo ? lo : (v > hi ? hi : v);
}

float pos(float v) {
    return v > 0.0f ? v : 0.0f;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && isspace((unsigned char)s[a])) {
        a++;
    }
    while (b > a && isspace((unsigned char)s[b - 1])) {
        b--;
    }
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

/* Printable, no characters that would break the [preset/name] section header. */
std::string clean_tune_name(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (c >= 0x20 && c != 0x7f && c != '/' && c != '[' && c != ']' && c != '=') {
            out += (char)c;
        }
    }
    out = trim(out);
    if (out.size() > kMaxTuneName) {
        out.resize(kMaxTuneName);
    }
    return out;
}

bool valid_preset_id(const std::string& id) {
    if (id.empty() || id == "custom") {
        return false;
    }
    for (unsigned char c : id) {
        if (!isalnum(c) && c != '-') {
            return false;
        }
    }
    return true;
}

}  // namespace

const FxField* tune_fields(int* count) {
    *count = (int)(sizeof(kTuneFields) / sizeof(kTuneFields[0]));
    return kTuneFields;
}

void tune_defaults(TuneParams* t) {
    memset(t, 0, sizeof(*t));
}

void tune_clamp(TuneParams* t) {
    t->pitch = clampf(t->pitch, -6.0f, 6.0f);
    t->character = clampf(t->character, -1.0f, 1.0f);
    t->body = clampf(t->body, -1.0f, 1.0f);
    t->presence = clampf(t->presence, -1.0f, 1.0f);
    t->mechanical = clampf(t->mechanical, -1.0f, 1.0f);
    t->helmet = clampf(t->helmet, -1.0f, 1.0f);
    t->saturation = clampf(t->saturation, -1.0f, 1.0f);
    t->wet = clampf(t->wet, -1.0f, 1.0f);
    t->output_db = clampf(t->output_db, -12.0f, 12.0f);
}

FxParams fx_apply_tuning(const FxParams& b, const TuneParams& tin) {
    TuneParams t = tin;
    tune_clamp(&t);
    FxParams e = b;
    if (!b.enabled) {
        return e;
    }
    /* Character sub-stages only count if the character block is on in the preset. */
    const bool bc = b.character_on;

    /* Pitch: pitch amount only. With the vocal model on, only the excitation
       moves, so the character's formants stay put. */
    e.pitch_semitones = (b.pitch_on ? b.pitch_semitones : 0.0f) + t.pitch;
    e.pitch_on = b.pitch_on || t.pitch != 0.0f;

    /* Character / formant: vocal-tract size and resonance. Voices without
       the vocal model blend it in from 0 as the control moves off centre. */
    const float cs = exp2f(0.25f * t.character);
    if (b.vt_on) {
        e.vt_formant = b.vt_formant * cs;
        e.vt_resonance = b.vt_resonance + 0.3f * t.character;
    } else {
        e.vt_on = t.character != 0.0f;
        e.vt_formant = cs;
        e.vt_resonance = 0.3f * t.character;
        e.vt_mix = fminf(1.0f, 4.0f * fabsf(t.character));
    }
    if (bc && b.vc_on) {
        e.formant_scale = b.formant_scale * cs;
    }

    /* Body: low end (high-pass corner), low/mid resonances, low shelf.
       Presence: upper-mid peak, top end (low-pass corner), upper resonances,
       high shelf, helmet top. */
    if (b.hp_on) {
        e.hp_freq = b.hp_freq * exp2f(-t.body);
    }
    if (b.peak_on) {
        e.peak_gain_db = b.peak_gain_db + 4.0f * t.presence;
    }
    if (b.lp_on) {
        e.lp_freq = b.lp_freq * exp2f(0.5f * t.presence);
    }
    float* res_freq[] = {&e.res1_freq, &e.res2_freq, &e.res3_freq, &e.res4_freq};
    float* res_q[] = {&e.res1_q, &e.res2_q, &e.res3_q, &e.res4_q};
    float* res_gain[] = {&e.res1_gain_db, &e.res2_gain_db, &e.res3_gain_db, &e.res4_gain_db};
    if (bc && b.res_on) {
        for (int i = 0; i < 4; i++) {
            if (*res_freq[i] < 1000.0f) {
                *res_gain[i] += 3.0f * t.body;
            } else if (*res_freq[i] >= 1500.0f) {
                *res_gain[i] += 3.0f * t.presence;
            }
            /* Mechanical: tighter, more metallic resonances. */
            *res_q[i] *= exp2f(0.5f * t.mechanical);
        }
    }
    if (bc && b.feq_on) {
        e.feq_low_db = b.feq_low_db + 4.0f * t.body;
        e.feq_high_db = b.feq_high_db + 4.0f * t.presence;
    } else {
        e.feq_on = true; /* 0 dB shelves are transparent */
        e.feq_low_hz = 180.0f;
        e.feq_low_db = 4.0f * t.body;
        e.feq_high_hz = 3500.0f;
        e.feq_high_db = 4.0f * t.presence;
    }

    /* Mechanical: ring modulation and metallic comb resonance. */
    const float m = t.mechanical;
    if (b.ring_on) {
        e.ring_mix = b.ring_mix * (1.0f + m);
    } else {
        e.ring_on = m > 0.0f;
        e.ring_freq = 80.0f;
        e.ring_mix = 0.25f * pos(m);
    }
    if (b.comb_on) {
        e.comb_mix = b.comb_mix * (1.0f + 0.6f * m);
    } else {
        e.comb_on = m > 0.0f;
        e.comb_ms = 1.2f;
        e.comb_feedback = 0.0f;
        e.comb_damp = 0.0f;
        e.comb_mix = 0.3f * pos(m);
    }

    /* Helmet / cavity: band limit, short reflections. Voices without a
       helmet get one that starts transparent and closes in. */
    const float h = t.helmet;
    if (bc && b.helmet_on) {
        e.helmet_low_hz = b.helmet_low_hz * exp2f(0.5f * h);
        e.helmet_high_hz = b.helmet_high_hz * exp2f(-0.4f * h + 0.3f * t.presence);
        e.helmet_reflect_mix = b.helmet_reflect_mix + 0.3f * h;
        e.helmet_reflect_fb = b.helmet_reflect_fb + 0.2f * h;
    } else {
        const float hp = pos(h);
        e.helmet_on = hp > 0.01f;
        e.helmet_low_hz = 60.0f + 300.0f * hp;
        e.helmet_high_hz = (12000.0f - 8500.0f * hp) * exp2f(0.3f * t.presence);
        e.helmet_reflect_ms = 1.0f;
        e.helmet_reflect_fb = 0.35f * hp;
        e.helmet_reflect_mix = 0.4f * hp;
        e.helmet_am_depth = 0.0f;
    }

    /* Saturation: the voice's own grit stage if it has one, else a tanh
       stage faded in from 0. */
    const float s = t.saturation;
    if (bc && b.sat_on) {
        e.sat_drive_db = b.sat_drive_db + 9.0f * s;
        e.sat_mix = b.sat_mix + 0.3f * s;
    } else if (b.clip_on) {
        e.clip_factor = b.clip_factor + (int)lroundf(3.0f * s);
        e.sat_on = false;
    } else {
        e.sat_on = true;
        e.sat_drive_db = 9.0f;
        e.sat_bias = 0.0f;
        e.sat_out_db = 0.0f;
        e.sat_mix = 0.5f * pos(s);
    }

    /* Voices without the character block get it with only the
       tuning-controlled stages, all transparent at 0. */
    if (!bc) {
        e.character_on = true;
        e.char_mix = 1.0f;
        e.vc_on = false;
        e.comp_on = false;
        e.res_on = false;
    }

    /* Wet / dry: how much of the character processing is mixed in. */
    const float f = 1.0f + t.wet;
    if (bc) {
        e.char_mix = b.char_mix * f;
    }
    e.vt_mix *= f;
    e.ring_mix *= f;
    e.comb_mix *= f;

    e.out_db = b.out_db + t.output_db;
    fx_clamp(&e);
    return e;
}

std::string tunings_path_for(const std::string& presets_path) {
    const size_t slash = presets_path.rfind('/');
    if (slash == std::string::npos) {
        return "tunings.ini";
    }
    return presets_path.substr(0, slash + 1) + "tunings.ini";
}

TuneStore::TuneStore(const std::string& path) : path_(path) {
    load();
}

TuneStore::Entry* TuneStore::find(const std::string& preset, const std::string& name) {
    for (Entry& e : entries_) {
        if (e.preset == preset && lower(e.name) == lower(name)) {
            return &e;
        }
    }
    return nullptr;
}

const TuneStore::Entry* TuneStore::find(const std::string& preset, const std::string& name) const {
    return const_cast<TuneStore*>(this)->find(preset, name);
}

std::vector<std::string> TuneStore::names(const std::string& preset) const {
    std::vector<std::string> out;
    for (const Entry& e : entries_) {
        if (e.preset == preset) {
            out.push_back(e.name);
        }
    }
    return out;
}

bool TuneStore::get(const std::string& preset, const std::string& name, TuneParams* out) const {
    if (name.empty()) {
        tune_defaults(out);
        return true;
    }
    const Entry* e = find(preset, name);
    if (!e) {
        return false;
    }
    *out = e->tune;
    return true;
}

std::string TuneStore::active(const std::string& preset) const {
    for (const Active& a : active_) {
        if (a.preset == preset) {
            return find(preset, a.name) ? a.name : "";
        }
    }
    return "";
}

bool TuneStore::set_active(const std::string& preset, const std::string& name, std::string* err) {
    if (!valid_preset_id(preset)) {
        *err = "Pick a voice first.";
        return false;
    }
    if (!name.empty() && !find(preset, name)) {
        *err = "Unknown tuning: " + name;
        return false;
    }
    const std::vector<Active> before = active_;
    bool found = false;
    for (Active& a : active_) {
        if (a.preset == preset) {
            a.name = name;
            found = true;
        }
    }
    if (!found) {
        active_.push_back({preset, name});
    }
    if (!write(err)) {
        active_ = before;
        return false;
    }
    return true;
}

std::string TuneStore::save(const std::string& preset, const std::string& raw_name, const TuneParams& t,
                            std::string* err) {
    if (!valid_preset_id(preset)) {
        *err = "Pick a voice first.";
        return "";
    }
    const std::string name = clean_tune_name(raw_name);
    if (name.empty()) {
        *err = "Enter a name for the tuning.";
        return "";
    }
    if (lower(name) == "default") {
        *err = "\"Default\" is the voice as designed; pick another name.";
        return "";
    }
    const std::vector<Entry> before = entries_;
    const std::vector<Active> before_active = active_;
    Entry* e = find(preset, name);
    if (!e) {
        entries_.push_back({preset, name, t});
        e = &entries_.back();
    }
    e->tune = t;
    tune_clamp(&e->tune);
    const std::string stored = e->name;
    bool found = false;
    for (Active& a : active_) {
        if (a.preset == preset) {
            a.name = stored;
            found = true;
        }
    }
    if (!found) {
        active_.push_back({preset, stored});
    }
    if (!write(err)) {
        entries_ = before;
        active_ = before_active;
        return "";
    }
    return stored;
}

bool TuneStore::remove(const std::string& preset, const std::string& name, std::string* err) {
    for (size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].preset == preset && lower(entries_[i].name) == lower(name)) {
            const std::vector<Entry> before = entries_;
            entries_.erase(entries_.begin() + (long)i);
            if (!write(err)) {
                entries_ = before;
                return false;
            }
            return true;
        }
    }
    *err = "Unknown tuning: " + name;
    return false;
}

/*
 * File format:
 *   [r3x]            active=Frank
 *   [r3x/Frank]      pitch=0.5, character=0.2, ...
 */
void TuneStore::load() {
    entries_.clear();
    active_.clear();
    FILE* f = fopen(path_.c_str(), "r");
    if (!f) {
        return;
    }
    char line[512];
    Entry* cur = nullptr;
    std::string section;
    while (fgets(line, sizeof(line), f)) {
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') {
            continue;
        }
        if (s[0] == '[' && s.back() == ']') {
            section = s.substr(1, s.size() - 2);
            cur = nullptr;
            const size_t slash = section.find('/');
            if (slash != std::string::npos) {
                const std::string preset = section.substr(0, slash);
                const std::string name = clean_tune_name(section.substr(slash + 1));
                if (valid_preset_id(preset) && !name.empty() && lower(name) != "default" &&
                    !find(preset, name)) {
                    Entry e;
                    e.preset = preset;
                    e.name = name;
                    tune_defaults(&e.tune);
                    entries_.push_back(e);
                    cur = &entries_.back();
                }
                section.clear();
            }
            continue;
        }
        const size_t eq = s.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));
        if (cur) {
            int count = 0;
            const FxField* fields = tune_fields(&count);
            for (int i = 0; i < count; i++) {
                if (key == fields[i].key) {
                    *(float*)((char*)&cur->tune + fields[i].offset) = strtof(value.c_str(), nullptr);
                }
            }
        } else if (!section.empty() && key == "active" && valid_preset_id(section)) {
            active_.push_back({section, clean_tune_name(value)});
        }
    }
    fclose(f);
    for (Entry& e : entries_) {
        tune_clamp(&e.tune);
    }
}

bool TuneStore::write(std::string* err) const {
    for (size_t p = 1; (p = path_.find('/', p)) != std::string::npos; p++) {
        if (mkdir(path_.substr(0, p).c_str(), 0755) < 0 && errno != EEXIST) {
            *err = "Cannot create folder for " + path_ + ": " + strerror(errno);
            return false;
        }
    }
    const std::string tmp = path_ + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) {
        *err = "Cannot write " + tmp + ": " + strerror(errno);
        return false;
    }
    fprintf(f, "# UVC VoiceChanger personal tunings (written by uvc_pass)\n");
    for (const Active& a : active_) {
        fprintf(f, "\n[%s]\nactive=%s\n", a.preset.c_str(), a.name.c_str());
    }
    int count = 0;
    const FxField* fields = tune_fields(&count);
    for (const Entry& e : entries_) {
        fprintf(f, "\n[%s/%s]\n", e.preset.c_str(), e.name.c_str());
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s=%g\n", fields[i].key, *(const float*)((const char*)&e.tune + fields[i].offset));
        }
    }
    const bool ok = fflush(f) == 0 && fclose(f) == 0;
    if (!ok || rename(tmp.c_str(), path_.c_str()) < 0) {
        *err = "Cannot save " + path_ + ": " + strerror(errno);
        ::remove(tmp.c_str());
        return false;
    }
    return true;
}
