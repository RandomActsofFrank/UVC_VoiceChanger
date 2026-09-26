/*
 * ALSA stereo capture -> voice effects -> playback.
 * Default: web page for devices and voice. Use --cli to start audio directly.
 */

#include "alsa_io.h"
#include "engine.h"
#include "presets.h"
#include "settings.h"
#include "web.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_run = 1;
static PassEngine* g_cli_engine = nullptr;

static void on_signal(int) {
    g_run = 0;
    if (g_cli_engine) {
        g_cli_engine->stop();
    }
}

static int run_cli(EngineConfig& cfg, PttMonitor* ptt) {
    cfg.rate = 48000;
    cfg.channels = 2;

    fprintf(stderr,
            "UVC Linux audio engine — CLI\n"
            "  format : 48000 Hz, S16_LE, 2 ch (strict)\n"
            "  period : %u  buffer: %u\n"
            "  input  : %s\n"
            "  output : %s\n"
            "  preset : %s\n",
            cfg.period_frames,
            cfg.buffer_frames,
            cfg.input_dev,
            cfg.output_dev,
            cfg.preset);

    PassEngine engine;
    FxParams fx;
    PresetStore(cfg.presets_path).get(cfg.preset, &fx);
    const TuneStore tunes(tunings_path_for(cfg.presets_path));
    TuneParams tune;
    if (!tunes.get(cfg.preset, tunes.active(cfg.preset), &tune)) {
        tune_defaults(&tune);
    }
    engine.set_voice(fx, tune);
    engine.set_ptt(ptt);
    g_cli_engine = &engine;
    if (!engine.start(cfg)) {
        fprintf(stderr, "Start failed: %s\n", engine.last_error().c_str());
        g_cli_engine = nullptr;
        return 1;
    }
    if (cfg.verbose) {
        fprintf(stderr, "%s\n", engine.status().c_str());
    }

    struct timespec last_log;
    clock_gettime(CLOCK_MONOTONIC, &last_log);
    while (g_run && engine.running()) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double dt = (now.tv_sec - last_log.tv_sec) + (now.tv_nsec - last_log.tv_nsec) / 1e9;
        if (cfg.verbose && dt >= 1.0) {
            fprintf(stderr, "running  periods=%llu  ptt=%s  pwm=%dus\n", (unsigned long long)engine.blocks(),
                    ptt_state_name(ptt->state()), ptt->pulse_us());
            last_log = now;
        }
        struct timespec sleep_ts = {0, 100000000L};
        nanosleep(&sleep_ts, nullptr);
    }

    if (!engine.last_error().empty()) {
        fprintf(stderr, "%s\n", engine.last_error().c_str());
    }
    engine.stop();
    g_cli_engine = nullptr;
    fprintf(stderr, "Stopped.\n");
    return 0;
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
    if (!cfg.presets_path[0]) {
        snprintf(cfg.presets_path, sizeof(cfg.presets_path), "%s", default_presets_path().c_str());
    }
    FxParams check;
    if (cfg.preset[0] && !PresetStore(cfg.presets_path).get(cfg.preset, &check)) {
        fprintf(stderr, "Unknown preset: %s\n", cfg.preset);
        return 1;
    }
    AppSettings settings;
    settings.load(settings_path_for(cfg.presets_path));
    if (!cfg.preset[0]) {
        const std::string saved = settings.startup_preset;
        const bool usable = !saved.empty() && PresetStore(cfg.presets_path).get(saved, &check);
        snprintf(cfg.preset, sizeof(cfg.preset), "%s", usable ? saved.c_str() : "clean");
    }

    /* Push-to-talk runs on its own thread for the life of the process,
       whether or not anyone has the web page open. */
    PttMonitor ptt;
    ptt.start(settings.ptt);
    if (settings.ptt.enabled) {
        fprintf(stderr, "Push-to-talk on: GPIO%d, ON >= %d us, OFF <= %d us, timeout %d ms (muted until pressed)\n",
                settings.ptt.gpio, settings.ptt.on_us, settings.ptt.off_us, settings.ptt.timeout_ms);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (cfg.use_cli) {
        return run_cli(cfg, &ptt);
    }
    return run_web(&cfg, &g_run, &ptt);
}
