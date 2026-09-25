#include "presets.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {

const size_t kMaxNameLen = 40;

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

std::string clean_name(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (c >= 0x20 && c != 0x7f) {
            out += (char)c;
        }
    }
    out = trim(out);
    if (out.size() > kMaxNameLen) {
        out.resize(kMaxNameLen);
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

std::string slug(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (isalnum(c)) {
            out += (char)tolower(c);
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    if (out.size() > 24) {
        out.resize(24);
    }
    return out.empty() ? "preset" : out;
}

bool mkdirs_for(const std::string& file) {
    for (size_t pos = 1; (pos = file.find('/', pos)) != std::string::npos; pos++) {
        const std::string dir = file.substr(0, pos);
        if (mkdir(dir.c_str(), 0755) < 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

void set_field(FxParams* fx, const std::string& key, const std::string& value) {
    int count = 0;
    const FxField* fields = fx_fields(&count);
    for (int i = 0; i < count; i++) {
        if (key != fields[i].key) {
            continue;
        }
        char* base = (char*)fx + fields[i].offset;
        switch (fields[i].kind) {
            case kFxBool:
                *(bool*)base = value == "1" || value == "true";
                break;
            case kFxInt:
                *(int*)base = atoi(value.c_str());
                break;
            case kFxFloat:
                *(float*)base = strtof(value.c_str(), nullptr);
                break;
        }
        return;
    }
}

}  // namespace

std::string default_presets_path() {
    const char* home = getenv("HOME");
    if (!home || !home[0]) {
        return "presets.ini";
    }
    return std::string(home) + "/.config/uvc-voicechanger/presets.ini";
}

PresetStore::PresetStore(const std::string& path) : path_(path) {
    load();
}

bool PresetStore::is_builtin(const std::string& id) const {
    FxParams tmp;
    return fx_apply_preset(id.c_str(), &tmp);
}

std::string PresetStore::builtin_name(const std::string& id) const {
    int count = 0;
    const FxPresetInfo* presets = fx_presets(&count);
    for (int i = 0; i < count; i++) {
        if (id == presets[i].id) {
            return presets[i].name;
        }
    }
    return id;
}

const PresetStore::Entry* PresetStore::find(const std::string& id) const {
    for (const Entry& e : saved_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

void PresetStore::load() {
    saved_.clear();
    FILE* f = fopen(path_.c_str(), "r");
    if (!f) {
        return;
    }
    char line[512];
    Entry* cur = nullptr;
    while (fgets(line, sizeof(line), f)) {
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') {
            continue;
        }
        if (s[0] == '[' && s.back() == ']') {
            const std::string id = slug(s.substr(1, s.size() - 2));
            if (id == "custom" || find(id)) {
                cur = nullptr;
                continue;
            }
            Entry e;
            e.id = id;
            e.name = builtin_name(id);
            if (!fx_apply_preset(id.c_str(), &e.fx)) {
                fx_apply_preset("clean", &e.fx);
            }
            saved_.push_back(e);
            cur = &saved_.back();
            continue;
        }
        const size_t eq = s.find('=');
        if (!cur || eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));
        if (key == "name") {
            const std::string name = clean_name(value);
            if (!name.empty() && !is_builtin(cur->id)) {
                cur->name = name;
            }
        } else {
            set_field(&cur->fx, key, value);
        }
    }
    fclose(f);
    for (Entry& e : saved_) {
        fx_clamp(&e.fx);
        snprintf(e.fx.preset, sizeof(e.fx.preset), "%s", e.id.c_str());
    }
}

bool PresetStore::write(std::string* err) const {
    if (!mkdirs_for(path_)) {
        *err = "Cannot create folder for " + path_ + ": " + strerror(errno);
        return false;
    }
    const std::string tmp = path_ + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) {
        *err = "Cannot write " + tmp + ": " + strerror(errno);
        return false;
    }
    fprintf(f, "# UVC VoiceChanger presets (written by uvc_pass)\n");
    int count = 0;
    const FxField* fields = fx_fields(&count);
    for (const Entry& e : saved_) {
        fprintf(f, "\n[%s]\nname=%s\n", e.id.c_str(), e.name.c_str());
        for (int i = 0; i < count; i++) {
            const char* base = (const char*)&e.fx + fields[i].offset;
            switch (fields[i].kind) {
                case kFxBool:
                    fprintf(f, "%s=%d\n", fields[i].key, *(const bool*)base ? 1 : 0);
                    break;
                case kFxInt:
                    fprintf(f, "%s=%d\n", fields[i].key, *(const int*)base);
                    break;
                case kFxFloat:
                    fprintf(f, "%s=%g\n", fields[i].key, *(const float*)base);
                    break;
            }
        }
    }
    const bool ok = fflush(f) == 0 && fclose(f) == 0;
    if (!ok || rename(tmp.c_str(), path_.c_str()) < 0) {
        *err = "Cannot save " + path_ + ": " + strerror(errno);
        remove(tmp.c_str());
        return false;
    }
    return true;
}

std::vector<PresetInfo> PresetStore::list() const {
    std::vector<PresetInfo> out;
    int count = 0;
    const FxPresetInfo* presets = fx_presets(&count);
    for (int i = 0; i < count; i++) {
        const Entry* e = find(presets[i].id);
        out.push_back({presets[i].id, presets[i].name, true, e != nullptr, presets[i].voice});
    }
    for (const Entry& e : saved_) {
        if (!is_builtin(e.id)) {
            out.push_back({e.id, e.name, false, false, true});
        }
    }
    return out;
}

bool PresetStore::get(const std::string& id, FxParams* out) const {
    if (const Entry* e = find(id)) {
        *out = e->fx;
        return true;
    }
    return fx_apply_preset(id.c_str(), out);
}

bool PresetStore::save(const std::string& id, const FxParams& fx, std::string* err) {
    if (!find(id) && !is_builtin(id)) {
        *err = "Unknown preset: " + id;
        return false;
    }
    const std::vector<Entry> before = saved_;
    Entry* e = const_cast<Entry*>(find(id));
    if (!e) {
        saved_.push_back({id, builtin_name(id), fx});
        e = &saved_.back();
    }
    e->fx = fx;
    fx_clamp(&e->fx);
    snprintf(e->fx.preset, sizeof(e->fx.preset), "%s", id.c_str());
    if (!write(err)) {
        saved_ = before;
        return false;
    }
    return true;
}

std::string PresetStore::save_as(const std::string& raw_name, const FxParams& fx, std::string* err) {
    const std::string name = clean_name(raw_name);
    if (name.empty()) {
        *err = "Enter a preset name.";
        return "";
    }
    for (const PresetInfo& p : list()) {
        if (lower(p.name) == lower(name)) {
            return save(p.id, fx, err) ? p.id : "";
        }
    }

    const std::string base = slug(name);
    std::string id = base;
    for (int n = 2; id == "custom" || is_builtin(id) || find(id); n++) {
        id = base + "-" + std::to_string(n);
    }
    const std::vector<Entry> before = saved_;
    saved_.push_back({id, name, fx});
    fx_clamp(&saved_.back().fx);
    snprintf(saved_.back().fx.preset, sizeof(saved_.back().fx.preset), "%s", id.c_str());
    if (!write(err)) {
        saved_ = before;
        return "";
    }
    return id;
}

bool PresetStore::reset(const std::string& id, std::string* err) {
    for (size_t i = 0; i < saved_.size(); i++) {
        if (saved_[i].id == id) {
            const std::vector<Entry> before = saved_;
            saved_.erase(saved_.begin() + (long)i);
            if (!write(err)) {
                saved_ = before;
                return false;
            }
            return true;
        }
    }
    if (is_builtin(id)) {
        return true; /* already at defaults */
    }
    *err = "Unknown preset: " + id;
    return false;
}
