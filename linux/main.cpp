/*
 * Milestone 1: ALSA stereo pass-through for Sound Blaster Play! 3.
 * No DSP. No mono downmix. Capture -> optional gain -> playback.
 */

#include "alsa_io.h"

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_run = 1;

static void on_signal(int) {
    g_run = 0;
}

static void apply_gain(int16_t* samples, size_t count, float gain) {
    if (gain == 1.0f) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        float v = (float)samples[i] * gain;
        if (v > 32767.0f) {
            v = 32767.0f;
        } else if (v < -32768.0f) {
            v = -32768.0f;
        }
        samples[i] = (int16_t)v;
    }
}

static void measure_levels(const int16_t* interleaved,
                           unsigned int frames,
                           unsigned int channels,
                           int* peak_out,
                           double* rms_out) {
    int peak = 0;
    double sumsq = 0.0;
    size_t n = (size_t)frames * channels;
    for (size_t i = 0; i < n; i++) {
        int v = interleaved[i];
        int a = v < 0 ? -v : v;
        if (a > peak) {
            peak = a;
        }
        sumsq += (double)v * (double)v;
    }
    *peak_out = peak;
    *rms_out = n ? sqrt(sumsq / (double)n) : 0.0;
}

int main(int argc, char** argv) {
    EngineConfig cfg;
    int parse = engine_parse_args(argc, argv, &cfg);
    if (parse > 0) {
        return 0;
    }
    if (parse < 0) {
        return 1;
    }

    if (cfg.list_only) {
        return alsa_list_devices() == 0 ? 0 : 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr,
            "UVC Linux audio engine — Milestone 1 (stereo pass-through)\n"
            "  requested: %u Hz, %u ch, period %u, buffer %u\n"
            "  mic-gain=%.3f  amp-gain=%.3f\n"
            "  input=%s  output=%s\n",
            cfg.rate,
            cfg.channels,
            cfg.period_frames,
            cfg.buffer_frames,
            cfg.mic_gain,
            cfg.amp_gain,
            cfg.input_dev,
            cfg.output_dev);

    AlsaDuplex* io = alsa_open(&cfg);
    if (!io) {
        return 1;
    }
    if (cfg.verbose) {
        alsa_print_info(io);
    }

    const unsigned int period = alsa_period_frames(io);
    const unsigned int channels = alsa_channels(io);
    const unsigned int rate = alsa_rate(io);
    size_t samples = (size_t)period * channels;
    int16_t* block = (int16_t*)calloc(samples, sizeof(int16_t));
    if (!block) {
        alsa_close(io);
        return 1;
    }

    fprintf(stderr, "Pass-through running (Ctrl+C to stop). Stereo channels preserved.\n");

    unsigned long long blocks = 0;
    struct timespec last_log;
    clock_gettime(CLOCK_MONOTONIC, &last_log);

    while (g_run) {
        if (alsa_read(io, block, period) < 0) {
            fprintf(stderr, "Capture failed — exiting\n");
            break;
        }
        apply_gain(block, samples, cfg.mic_gain);
        apply_gain(block, samples, cfg.amp_gain);
        if (alsa_write(io, block, period) < 0) {
            fprintf(stderr, "Playback failed — exiting\n");
            break;
        }
        blocks++;

        if (cfg.verbose) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double dt = (now.tv_sec - last_log.tv_sec) + (now.tv_nsec - last_log.tv_nsec) / 1e9;
            if (dt >= 1.0) {
                int peak = 0;
                double rms = 0.0;
                measure_levels(block, period, channels, &peak, &rms);
                double peak_db = peak > 0 ? 20.0 * log10((double)peak / 32768.0) : -120.0;
                double rms_db = rms > 0.0 ? 20.0 * log10(rms / 32768.0) : -120.0;
                fprintf(stderr,
                        "levels  peak=%5d (%.1f dBFS)  rms=%.1f dBFS  blocks=%llu  %u Hz %u ch\n",
                        peak,
                        peak_db,
                        rms_db,
                        blocks,
                        rate,
                        channels);
                last_log = now;
            }
        }
    }

    free(block);
    alsa_close(io);
    fprintf(stderr, "Stopped after %llu periods.\n", blocks);
    return 0;
}
