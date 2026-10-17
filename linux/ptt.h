#pragma once

/*
 * Push-to-talk input (RC PWM or a physical GPIO button) and the output gate.
 *
 *   GPIO -> PttMonitor (own thread) -> lock-free state -> OutputGate in audio
 *
 * Sources (pick one in settings):
 *   rc     — RC receiver PWM channel; pulse-width thresholds with hysteresis
 *   button — momentary switch on a GPIO; held = talk, released = mute
 *
 * Fail-safe (both sources): output is allowed only while PTT is enabled AND
 * the monitor is healthy AND the input says "talk". The audio side ages the
 * last healthy update itself, so a dead GPIO, stalled thread or missing RC
 * signal always reads as "signal lost" -> muted — never the last known state.
 */

#include <stdint.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

enum PttState {
    kPttDisabled = 0,   /* PTT off in settings: output always allowed */
    kPttSignalLost = 1, /* no healthy updates within the timeout: muted */
    kPttInactive = 2,   /* healthy, button/RC released: muted */
    kPttActive = 3,     /* healthy, button/RC held: output allowed */
};

enum PttSource {
    kPttSourceRc = 0,
    kPttSourceButton = 1,
};

const char* ptt_state_name(int state);
const char* ptt_source_name(int source);
int ptt_source_from_name(const char* name); /* unknown -> rc */

struct PttConfig {
    bool enabled = false;
    int source = kPttSourceRc;
    std::string chip;    /* "" = auto-detect the 40-pin header's GPIO chip */
    int gpio = 17;       /* BCM GPIO number (header pin 11) */
    /* RC PWM: */
    int on_us = 1700;    /* pulse at/over this -> ON */
    int off_us = 1300;   /* pulse at/under this -> OFF; in between keeps state */
    int timeout_ms = 100;
    int min_us = 700;    /* pulses outside min..max are invalid -> muted */
    int max_us = 2300;
    /* Physical button: */
    bool active_low = false; /* false = press pulls pin high (to 3.3 V); true = press to GND */
    int debounce_ms = 25;
};

void ptt_clamp(PttConfig* cfg);
/* "" if usable, otherwise what's wrong (for the settings page). */
std::string ptt_validate(const PttConfig& cfg);

/* Pure pulse decoder (no I/O, unit-tested in ptt_test.cpp).
 * With on_us < off_us the sense is inverted (short pulse = ON), for channels
 * whose button moves the other way. */
class PttDecoder {
public:
    static const int kPulsesToLock = 3; /* consecutive valid pulses before trusting the signal */

    void configure(const PttConfig& cfg);
    void reset();     /* signal lost: forget everything, back to OFF */
    void drop_edge(); /* missed edge(s): don't pair across the gap */
    /* Returns true when a falling edge completes a pulse. */
    bool edge(bool rising, int64_t t_ns);

    bool locked() const { return locked_; }
    bool active() const { return locked_ && level_; }
    bool last_valid() const { return last_valid_; }
    int last_us() const { return last_us_; }

private:
    int on_us_ = 1700;
    int off_us_ = 1300;
    int min_us_ = 700;
    int max_us_ = 2300;
    bool have_rise_ = false;
    int64_t rise_ns_ = 0;
    int good_ = 0;
    bool locked_ = false;
    bool level_ = false;
    bool last_valid_ = false;
    int last_us_ = 0;
};

/* State from the monitor's published values (pure; unit-tested). */
int ptt_state_from(bool enabled, int64_t last_valid_ns, int64_t now_ns, int64_t timeout_ns, bool active);

/* Owns the GPIO line and a small monitor thread. Independent of the web page
 * and of the audio engine: start() it once at launch, again on settings change. */
class PttMonitor {
public:
    ~PttMonitor() { stop(); }

    void start(const PttConfig& cfg); /* (re)start; muted until the source says talk */
    void stop();

    /* Lock-free; safe from the audio thread. */
    int state() const;
    bool output_allowed() const {
        const int s = state();
        return s == kPttDisabled || s == kPttActive;
    }

    int pulse_us() const { return pulse_us_.load(std::memory_order_relaxed); }
    PttConfig config() const;
    std::string chip() const;  /* GPIO chip in use, e.g. "/dev/gpiochip0 (pinctrl-bcm2835)" */
    std::string error() const; /* "" when the GPIO is open and fine */

private:
    void loop(PttConfig cfg);
    void loop_rc(PttConfig cfg);
    void loop_button(PttConfig cfg);
    void set_error(const std::string& e);

    std::atomic<bool> enabled_{false};
    std::atomic<bool> run_{false};
    std::atomic<int64_t> last_valid_ns_{0}; /* CLOCK_MONOTONIC of last healthy update; 0 = none */
    std::atomic<bool> active_{false};
    std::atomic<int64_t> timeout_ns_{100000000};
    std::atomic<int> pulse_us_{0};
    std::thread thread_;
    mutable std::mutex mu_;
    PttConfig cfg_;
    std::string chip_;
    std::string error_;
};

/* Smooth output mute/unmute at the very end of the chain (no ALSA changes).
 * Linear gain ramp; when fully open the samples are left untouched. */
class OutputGate {
public:
    void init(unsigned int rate, float ramp_ms);
    void process(int16_t* interleaved, unsigned int frames, unsigned int channels, bool open);
    float gain() const { return gain_ < 0.0f ? 1.0f : gain_; }

private:
    float step_ = 0.01f;
    float gain_ = -1.0f; /* < 0 until the first block: start at the target, no ramp */
};

int64_t ptt_now_ns();
