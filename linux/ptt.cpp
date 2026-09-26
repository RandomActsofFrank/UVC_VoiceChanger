#include "ptt.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/gpio.h>
#include <sys/ioctl.h>
#endif
#if defined(GPIO_V2_GET_LINE_IOCTL)
#define PTT_HAVE_GPIO_V2 1
#endif

int64_t ptt_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

const char* ptt_state_name(int state) {
    switch (state) {
        case kPttDisabled: return "disabled";
        case kPttSignalLost: return "lost";
        case kPttInactive: return "ready";
        case kPttActive: return "talking";
    }
    return "lost";
}

namespace {

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void ptt_clamp(PttConfig* cfg) {
    cfg->gpio = clampi(cfg->gpio, 0, 53);
    cfg->on_us = clampi(cfg->on_us, 100, 5000);
    cfg->off_us = clampi(cfg->off_us, 100, 5000);
    cfg->timeout_ms = clampi(cfg->timeout_ms, 20, 2000);
    cfg->min_us = clampi(cfg->min_us, 100, 5000);
    cfg->max_us = clampi(cfg->max_us, 100, 5000);
}

std::string ptt_validate(const PttConfig& cfg) {
    if (cfg.min_us >= cfg.max_us) {
        return "Valid pulse range: minimum must be below maximum.";
    }
    if (cfg.on_us == cfg.off_us) {
        return "ON and OFF thresholds must differ (the gap between them is the hysteresis).";
    }
    if (cfg.on_us < cfg.min_us || cfg.on_us > cfg.max_us || cfg.off_us < cfg.min_us || cfg.off_us > cfg.max_us) {
        return "ON and OFF thresholds must be inside the valid pulse range.";
    }
    return "";
}

/* ---- PttDecoder ---- */

void PttDecoder::configure(const PttConfig& cfg) {
    on_us_ = cfg.on_us;
    off_us_ = cfg.off_us;
    min_us_ = cfg.min_us;
    max_us_ = cfg.max_us;
    reset();
}

void PttDecoder::reset() {
    have_rise_ = false;
    good_ = 0;
    locked_ = false;
    level_ = false;
    last_valid_ = false;
    last_us_ = 0;
}

void PttDecoder::drop_edge() {
    have_rise_ = false;
}

bool PttDecoder::edge(bool rising, int64_t t_ns) {
    if (rising) {
        rise_ns_ = t_ns;
        have_rise_ = true;
        return false;
    }
    if (!have_rise_) {
        return false;
    }
    have_rise_ = false;
    const int64_t width = (t_ns - rise_ns_) / 1000;
    last_us_ = width > 100000 ? 100000 : (width < 0 ? 0 : (int)width);
    last_valid_ = last_us_ >= min_us_ && last_us_ <= max_us_;
    if (!last_valid_) {
        /* Out-of-range pulse: distrust the signal until it settles again. */
        good_ = 0;
        locked_ = false;
        level_ = false;
        return true;
    }
    if (on_us_ > off_us_) {
        if (last_us_ >= on_us_) {
            level_ = true;
        } else if (last_us_ <= off_us_) {
            level_ = false;
        }
    } else {
        if (last_us_ <= on_us_) {
            level_ = true;
        } else if (last_us_ >= off_us_) {
            level_ = false;
        }
    }
    if (good_ < kPulsesToLock) {
        good_++;
    }
    locked_ = good_ >= kPulsesToLock;
    return true;
}

int ptt_state_from(bool enabled, int64_t last_valid_ns, int64_t now_ns, int64_t timeout_ns, bool active) {
    if (!enabled) {
        return kPttDisabled;
    }
    if (last_valid_ns == 0 || now_ns - last_valid_ns > timeout_ns || now_ns < last_valid_ns) {
        return kPttSignalLost;
    }
    return active ? kPttActive : kPttInactive;
}

/* ---- GPIO access (Linux GPIO character device, uAPI v2) ---- */

namespace {

#if PTT_HAVE_GPIO_V2

/* The 40-pin header's controller: Pi Zero 2 W / 3 (bcm2835 driver), Pi 4,
   Pi 5 (RP1; its chip number differs between kernel versions). */
const char* const kHeaderChipLabels[] = {"pinctrl-bcm2835", "pinctrl-bcm2711", "pinctrl-rp1"};

bool chip_label(const char* path, std::string* label, unsigned int* lines) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct gpiochip_info info;
    memset(&info, 0, sizeof(info));
    const bool ok = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0;
    close(fd);
    if (ok) {
        info.label[sizeof(info.label) - 1] = '\0';
        *label = info.label;
        *lines = info.lines;
    }
    return ok;
}

std::string find_header_chip(std::string* label) {
    for (int i = 0; i < 32; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
        unsigned int lines = 0;
        if (!chip_label(path, label, &lines)) {
            continue;
        }
        for (const char* want : kHeaderChipLabels) {
            if (*label == want) {
                return path;
            }
        }
    }
    label->clear();
    return "/dev/gpiochip0";
}

/* Returns the line fd (edge events on both edges, pull-down), or -1. */
int open_line(const std::string& chip, int offset, std::string* err) {
    const int cfd = open(chip.c_str(), O_RDWR | O_CLOEXEC);
    if (cfd < 0) {
        const int e = errno;
        *err = "Cannot open " + chip + ": " + strerror(e);
        if (e == EACCES) {
            *err += " (add your user to the gpio group: sudo usermod -aG gpio $USER, then reboot)";
        }
        return -1;
    }
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = (uint32_t)offset;
    req.num_lines = 1;
    snprintf(req.consumer, sizeof(req.consumer), "%s", "uvc-ptt");
    /* Pull-down: an unplugged or unpowered receiver reads as a steady low
       (no pulses), which the timeout turns into "signal lost". */
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING |
                       GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    req.event_buffer_size = 64;
    if (ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        const int e = errno;
        char buf[160];
        snprintf(buf, sizeof(buf), "Cannot use GPIO%d on %s: %s", offset, chip.c_str(), strerror(e));
        *err = buf;
        if (e == EBUSY) {
            *err += " (already used by another driver or program; check /boot/firmware/config.txt overlays)";
        }
        close(cfd);
        return -1;
    }
    close(cfd);
    return req.fd;
}

#endif

}  // namespace

/* ---- PttMonitor ---- */

void PttMonitor::start(const PttConfig& in) {
    stop();
    PttConfig cfg = in;
    ptt_clamp(&cfg);
    last_valid_ns_.store(0);
    active_.store(false);
    pulse_us_.store(0);
    timeout_ns_.store((int64_t)cfg.timeout_ms * 1000000);
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg_ = cfg;
        chip_.clear();
        error_.clear();
    }
    enabled_.store(cfg.enabled);
    if (!cfg.enabled) {
        return;
    }
    run_.store(true);
    thread_ = std::thread([this, cfg]() { loop(cfg); });
}

void PttMonitor::stop() {
    run_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    last_valid_ns_.store(0);
    active_.store(false);
}

int PttMonitor::state() const {
    const int64_t last = last_valid_ns_.load(std::memory_order_acquire);
    const bool active = active_.load(std::memory_order_acquire);
    return ptt_state_from(enabled_.load(std::memory_order_relaxed), last, ptt_now_ns(),
                          timeout_ns_.load(std::memory_order_relaxed), active);
}

PttConfig PttMonitor::config() const {
    std::lock_guard<std::mutex> lock(mu_);
    return cfg_;
}

std::string PttMonitor::chip() const {
    std::lock_guard<std::mutex> lock(mu_);
    return chip_;
}

std::string PttMonitor::error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

void PttMonitor::set_error(const std::string& e) {
    std::lock_guard<std::mutex> lock(mu_);
    if (e != error_ && !e.empty()) {
        fprintf(stderr, "PTT: %s\n", e.c_str());
    }
    error_ = e;
}

void PttMonitor::loop(PttConfig cfg) {
#if PTT_HAVE_GPIO_V2
    PttDecoder dec;
    dec.configure(cfg);
    const int64_t timeout_ns = (int64_t)cfg.timeout_ms * 1000000;
    int fd = -1;
    uint32_t last_seq = 0;
    int64_t last_pulse_ns = 0; /* last valid pulse */
    int64_t last_any_ns = 0;   /* last pulse of any width (for the PWM readout) */

    auto lose = [&]() {
        active_.store(false, std::memory_order_release);
        last_valid_ns_.store(0, std::memory_order_release);
        pulse_us_.store(0, std::memory_order_relaxed);
        dec.reset();
    };

    while (run_.load()) {
        if (fd < 0) {
            lose();
            std::string label;
            const std::string chip = cfg.chip.empty() ? find_header_chip(&label) : cfg.chip;
            std::string err;
            fd = open_line(chip, cfg.gpio, &err);
            if (fd < 0) {
                set_error(err);
                for (int i = 0; i < 20 && run_.load(); i++) { /* retry every 2 s */
                    usleep(100000);
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                chip_ = label.empty() ? chip : chip + " (" + label + ")";
            }
            set_error("");
            fprintf(stderr, "PTT: monitoring GPIO%d on %s\n", cfg.gpio, chip.c_str());
            last_seq = 0;
            last_pulse_ns = last_any_ns = ptt_now_ns();
        }

        struct pollfd p = {fd, POLLIN, 0};
        const int r = poll(&p, 1, 20);
        if (r < 0 && errno != EINTR) {
            set_error(std::string("GPIO poll failed: ") + strerror(errno));
            close(fd);
            fd = -1;
            continue;
        }
        if (r > 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            set_error("GPIO line went away");
            close(fd);
            fd = -1;
            continue;
        }
        if (r > 0 && (p.revents & POLLIN)) {
            struct gpio_v2_line_event ev[16];
            const ssize_t n = read(fd, ev, sizeof(ev));
            if (n < 0 && errno != EINTR && errno != EAGAIN) {
                set_error(std::string("GPIO read failed: ") + strerror(errno));
                close(fd);
                fd = -1;
                continue;
            }
            const int count = n > 0 ? (int)(n / (ssize_t)sizeof(ev[0])) : 0;
            for (int i = 0; i < count; i++) {
                if (last_seq != 0 && ev[i].line_seqno != last_seq + 1) {
                    dec.drop_edge(); /* kernel buffer overflowed: don't pair across the gap */
                }
                last_seq = ev[i].line_seqno;
                const bool rising = ev[i].id == GPIO_V2_LINE_EVENT_RISING_EDGE;
                if (!dec.edge(rising, (int64_t)ev[i].timestamp_ns)) {
                    continue;
                }
                const int64_t now = ptt_now_ns();
                pulse_us_.store(dec.last_us(), std::memory_order_relaxed);
                last_any_ns = now;
                if (dec.last_valid()) {
                    last_pulse_ns = now;
                }
                if (dec.locked()) {
                    active_.store(dec.active(), std::memory_order_release);
                    last_valid_ns_.store(now, std::memory_order_release);
                } else {
                    active_.store(false, std::memory_order_release);
                    last_valid_ns_.store(0, std::memory_order_release);
                }
            }
        }
        /* Timeout on this side too (the audio side checks independently). */
        const int64_t now = ptt_now_ns();
        if (now - last_pulse_ns > timeout_ns) {
            active_.store(false, std::memory_order_release);
            last_valid_ns_.store(0, std::memory_order_release);
            dec.reset();
            last_pulse_ns = now; /* re-arm; stays lost until kPulsesToLock valid pulses */
        }
        if (now - last_any_ns > timeout_ns) {
            pulse_us_.store(0, std::memory_order_relaxed);
        }
    }
    if (fd >= 0) {
        close(fd);
    }
#else
    (void)cfg;
    set_error("GPIO edge events need the Linux GPIO v2 API (kernel 5.10 or newer headers)");
    while (run_.load()) {
        usleep(100000);
    }
#endif
    active_.store(false);
    last_valid_ns_.store(0);
}

/* ---- OutputGate ---- */

void OutputGate::init(unsigned int rate, float ramp_ms) {
    const float n = ramp_ms * 0.001f * (float)rate;
    step_ = n > 1.0f ? 1.0f / n : 1.0f;
    gain_ = -1.0f;
}

void OutputGate::process(int16_t* interleaved, unsigned int frames, unsigned int channels, bool open) {
    const float target = open ? 1.0f : 0.0f;
    if (gain_ < 0.0f) {
        gain_ = target;
    }
    if (gain_ == target) {
        if (!open) {
            memset(interleaved, 0, (size_t)frames * channels * sizeof(int16_t));
        }
        return;
    }
    for (unsigned int f = 0; f < frames; f++) {
        if (open) {
            gain_ = gain_ + step_ >= 1.0f ? 1.0f : gain_ + step_;
        } else {
            gain_ = gain_ - step_ <= 0.0f ? 0.0f : gain_ - step_;
        }
        int16_t* s = &interleaved[(size_t)f * channels];
        for (unsigned int k = 0; k < channels; k++) {
            s[k] = (int16_t)lrintf((float)s[k] * gain_);
        }
    }
}
