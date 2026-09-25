#pragma once

#include "dsp.h"

#include <string>
#include <vector>

struct PresetInfo {
    std::string id;
    std::string name;
    bool builtin;  /* ships with the app; can be restored to defaults */
    bool modified; /* builtin with saved changes */
};

/*
 * Built-in presets (dsp.cpp) plus user edits and user presets, persisted to an
 * INI-style file: one [id] section per saved preset, "name=" plus FxParams keys.
 */
class PresetStore {
public:
    explicit PresetStore(const std::string& path);

    const std::string& path() const { return path_; }
    std::vector<PresetInfo> list() const;
    bool get(const std::string& id, FxParams* out) const;
    bool is_builtin(const std::string& id) const;

    /* Overwrite an existing preset (builtin or user) with fx. */
    bool save(const std::string& id, const FxParams& fx, std::string* err);
    /* Save under a name: overwrites a preset with that name, else creates one. Returns id or "". */
    std::string save_as(const std::string& name, const FxParams& fx, std::string* err);
    /* Builtin: drop saved changes. User preset: delete it. */
    bool reset(const std::string& id, std::string* err);

private:
    struct Entry {
        std::string id;
        std::string name;
        FxParams fx;
    };

    void load();
    bool write(std::string* err) const;
    const Entry* find(const std::string& id) const;
    std::string builtin_name(const std::string& id) const;

    std::string path_;
    std::vector<Entry> saved_; /* builtin overrides and user presets, in file order */
};

std::string default_presets_path();
