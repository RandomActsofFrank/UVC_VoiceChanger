#include "engine.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

PassEngine::PassEngine() = default;

PassEngine::~PassEngine() {
    stop();
}

bool PassEngine::running() const {
    return running_.load();
}

unsigned long long PassEngine::blocks() const {
    return blocks_.load();
}

unsigned long long PassEngine::xruns() const {
    return xruns_.load();
}

std::string PassEngine::status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

std::string PassEngine::last_error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

bool PassEngine::start(const EngineConfig& cfg) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg_ = cfg;
        cfg_.rate = 48000;
        cfg_.channels = 2;
        error_.clear();
        status_ = "Opening ALSA devices…";
        blocks_ = 0;
        xruns_ = 0;
    }

    AlsaDuplex* io = alsa_open(&cfg_);
    if (!io) {
        std::lock_guard<std::mutex> lock(mu_);
        error_ = alsa_last_error();
        status_ = "Failed to open devices";
        return false;
    }
    if (alsa_rate(io) != 48000 || alsa_channels(io) != 2) {
        std::lock_guard<std::mutex> lock(mu_);
        error_ = "Device did not accept 48000 Hz / 2 channels";
        status_ = "Format rejected";
        alsa_close(io);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        io_ = io;
        status_ = "Running stereo pass-through (L→L, R→R)";
    }
    run_ = true;
    running_ = true;
    thread_ = std::thread([this]() { loop(); });
    return true;
}

void PassEngine::stop() {
    run_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (io_) {
        alsa_close(io_);
        io_ = nullptr;
    }
    running_ = false;
    if (status_ == "Running stereo pass-through (L→L, R→R)" || status_.find("Running") == 0) {
        status_ = "Stopped";
    }
}

void PassEngine::loop() {
    AlsaDuplex* io = nullptr;
    EngineConfig cfg{};
    {
        std::lock_guard<std::mutex> lock(mu_);
        io = io_;
        cfg = cfg_;
    }
    if (!io) {
        running_ = false;
        return;
    }

    const unsigned int period = alsa_period_frames(io);
    const unsigned int channels = alsa_channels(io);
    const size_t samples = (size_t)period * channels;
    int16_t* block = (int16_t*)calloc(samples, sizeof(int16_t));
    if (!block) {
        std::lock_guard<std::mutex> lock(mu_);
        error_ = "Out of memory for audio buffer";
        status_ = "Failed";
        running_ = false;
        return;
    }

    while (run_.load()) {
        if (alsa_read(io, block, period) < 0) {
            std::lock_guard<std::mutex> lock(mu_);
            error_ = alsa_last_error()[0] ? alsa_last_error() : "ALSA capture failed";
            status_ = "Capture error — stopped";
            xruns_++;
            break;
        }

        /* Direct copy path: optional linear gain only. No DSP, no channel remix. */
        if (cfg.mic_gain != 1.0f || cfg.amp_gain != 1.0f) {
            const float g = cfg.mic_gain * cfg.amp_gain;
            for (size_t i = 0; i < samples; i++) {
                float v = (float)block[i] * g;
                if (v > 32767.0f) {
                    v = 32767.0f;
                } else if (v < -32768.0f) {
                    v = -32768.0f;
                }
                block[i] = (int16_t)v;
            }
        }

        if (alsa_write(io, block, period) < 0) {
            std::lock_guard<std::mutex> lock(mu_);
            error_ = alsa_last_error()[0] ? alsa_last_error() : "ALSA playback failed";
            status_ = "Playback error — stopped";
            xruns_++;
            break;
        }
        blocks_++;
    }

    free(block);
    running_ = false;
}
