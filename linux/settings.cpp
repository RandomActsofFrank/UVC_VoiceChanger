#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace {

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

/* Single-line values only; keeps the file parseable. */
std::string one_line(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c != 0x7f) {
            out += (char)c;
        }
    }
    return out;
}

}  // namespace

std::string settings_path_for(const std::string& presets_path) {
    const size_t slash = presets_path.rfind('/');
    if (slash == std::string::npos) {
        return "settings.ini";
    }
    return presets_path.substr(0, slash + 1) + "settings.ini";
}

bool AppSettings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        const std::string s = trim(line);
        const size_t eq = s.find('=');
        if (s.empty() || s[0] == '#' || eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));
        if (key == "autostart") {
            autostart = value == "1" || value == "true";
        } else if (key == "input") {
            input = value;
        } else if (key == "output") {
            output = value;
        } else if (key == "startup_preset") {
            startup_preset = value;
        }
    }
    fclose(f);
    return true;
}

bool AppSettings::save(const std::string& path, std::string* err) const {
    for (size_t pos = 1; (pos = path.find('/', pos)) != std::string::npos; pos++) {
        if (mkdir(path.substr(0, pos).c_str(), 0755) < 0 && errno != EEXIST) {
            *err = "Cannot create folder for " + path + ": " + strerror(errno);
            return false;
        }
    }
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) {
        *err = "Cannot write " + tmp + ": " + strerror(errno);
        return false;
    }
    fprintf(f,
            "# UVC VoiceChanger settings (written by uvc_pass)\n"
            "autostart=%d\n"
            "input=%s\n"
            "output=%s\n"
            "startup_preset=%s\n",
            autostart ? 1 : 0,
            one_line(input).c_str(),
            one_line(output).c_str(),
            one_line(startup_preset).c_str());
    const bool ok = fflush(f) == 0 && fclose(f) == 0;
    if (!ok || rename(tmp.c_str(), path.c_str()) < 0) {
        *err = "Cannot save " + path + ": " + strerror(errno);
        remove(tmp.c_str());
        return false;
    }
    return true;
}
