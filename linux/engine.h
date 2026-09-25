#pragma once

#include "alsa_io.h"
#include "dsp.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

/* Threaded stereo capture -> voice effects -> playback. */
class PassEngine {
public:
    PassEngine();
    ~PassEngine();

    bool start(const EngineConfig& cfg);
    void stop();
    bool running() const;

    /* Safe to call while running; applied at the next audio period. */
    void set_fx(const FxParams& fx);
    FxParams fx() const;
    /* A/B listening mode (FxMode); not saved. */
    void set_mode(int mode);
    int mode() const { return mode_.load(); }

    std::string status() const;
    std::string last_error() const;
    unsigned long long blocks() const;
    unsigned long long xruns() const;

private:
    void loop();

    mutable std::mutex mu_;
    std::thread thread_;
    std::atomic<bool> run_{false};
    std::atomic<bool> running_{false};
    EngineConfig cfg_{};
    AlsaDuplex* io_ = nullptr;
    std::string status_;
    std::string error_;
    std::atomic<unsigned long long> blocks_{0};
    std::atomic<unsigned long long> xruns_{0};
    FxParams fx_{};
    std::atomic<unsigned int> fx_version_{0};
    std::atomic<int> mode_{kFxModeCharacter};
};
