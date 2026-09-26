#pragma once

#include "ptt.h"

#include <string>

/* App settings persisted next to presets.ini (settings.ini, key=value). */
struct AppSettings {
    bool autostart = false;     /* start audio when uvc_pass launches */
    std::string input;          /* devices used for autostart */
    std::string output;
    std::string startup_preset; /* empty = clean */
    PttConfig ptt;              /* push-to-talk (ptt_* keys) */

    bool load(const std::string& path);
    bool save(const std::string& path, std::string* err) const;
};

/* settings.ini in the same folder as the presets file. */
std::string settings_path_for(const std::string& presets_path);
