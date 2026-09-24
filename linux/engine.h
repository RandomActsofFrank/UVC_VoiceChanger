#pragma once

#include "alsa_io.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

/* Threaded stereo pass-through. DSP is intentionally absent. */
class PassEngine {
public:
    PassEngine();
    ~PassEngine();

    bool start(const EngineConfig& cfg);
    void stop();
    bool running() const;

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
};
