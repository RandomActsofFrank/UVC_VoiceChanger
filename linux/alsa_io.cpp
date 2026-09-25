/*
 * ALSA duplex capture/playback for the UVC Linux audio engine.
 * Milestone 1: stereo pass-through. No DSP, no downmix.
 */

#include "alsa_io.h"

#include <alsa/asoundlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AlsaDuplex {
    snd_pcm_t* capture;
    snd_pcm_t* playback;
    unsigned int rate;
    unsigned int channels;
    unsigned int period_frames;
    unsigned int buffer_frames;
    char input_name[128];
    char output_name[128];
};

static char g_last_error[512] = {0};

static void set_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", g_last_error);
}

const char* alsa_last_error(void) {
    return g_last_error[0] ? g_last_error : "";
}

void engine_config_defaults(EngineConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->input_dev, sizeof(cfg->input_dev), "%s", "default");
    snprintf(cfg->output_dev, sizeof(cfg->output_dev), "%s", "default");
    cfg->rate = 48000;
    cfg->channels = 2;
    cfg->period_frames = 256;
    cfg->buffer_frames = 1024;
    cfg->mic_gain = 1.0f;
    cfg->amp_gain = 1.0f;
    cfg->list_only = 0;
    cfg->verbose = 1;
    cfg->use_cli = 0;
    cfg->web_port = 8080;
}

static int looks_like_play3(const char* name) {
    if (!name) {
        return 0;
    }
    char lower[256];
    size_t n = strlen(name);
    if (n >= sizeof(lower)) {
        n = sizeof(lower) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    lower[n] = '\0';
    return strstr(lower, "sound blaster") != NULL ||
           strstr(lower, "play! 3") != NULL ||
           strstr(lower, "play 3") != NULL;
}

static void try_autofill_play3(EngineConfig* cfg) {
    if (strcmp(cfg->input_dev, "default") != 0 || strcmp(cfg->output_dev, "default") != 0) {
        return;
    }

    void** hints = NULL;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0 || !hints) {
        return;
    }

    char found[128] = {0};
    for (void** h = hints; *h; h++) {
        char* name = snd_device_name_get_hint(*h, "NAME");
        char* desc = snd_device_name_get_hint(*h, "DESC");
        if (name && (looks_like_play3(name) || looks_like_play3(desc))) {
            if (strncmp(name, "plughw:", 7) == 0 || strncmp(name, "hw:", 3) == 0) {
                snprintf(found, sizeof(found), "%s", name);
            } else if (found[0] == '\0') {
                snprintf(found, sizeof(found), "%s", name);
            }
        }
        free(name);
        free(desc);
        if (found[0] && strncmp(found, "plughw:", 7) == 0) {
            break;
        }
    }
    snd_device_name_free_hint(hints);

    if (found[0]) {
        snprintf(cfg->input_dev, sizeof(cfg->input_dev), "%s", found);
        snprintf(cfg->output_dev, sizeof(cfg->output_dev), "%s", found);
        fprintf(stderr, "Auto-selected Play! 3 style device: %s\n", found);
    }
}

void engine_print_usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  (no flags)             Serve the device-selection web page\n"
            "  --port N               Web page port (default: 8080)\n"
            "  --cli                  Start pass-through immediately, no web page\n"
            "  --list                 List ALSA PCM devices and exit\n"
            "  --input DEV            Capture device\n"
            "  --output DEV           Playback device\n"
            "  --rate N               Sample rate Hz (default/required: 48000)\n"
            "  --channels N           Channels (default/required: 2)\n"
            "  --period N             Period size in frames (default: 256)\n"
            "  --buffer N             Buffer size in frames (default: 1024)\n"
            "  --mic-gain F           Input linear gain (default: 1.0)\n"
            "  --amp-gain F           Output linear gain (default: 1.0)\n"
            "  --quiet                Less logging\n"
            "  -h, --help             This help\n"
            "\n"
            "Milestone 1: ALSA stereo pass-through only. No DSP.\n"
            "Format is strict: S16_LE, requested rate, requested channels.\n",
            argv0);
}

int engine_parse_args(int argc, char** argv, EngineConfig* cfg) {
    engine_config_defaults(cfg);
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--list")) {
            cfg->list_only = 1;
        } else if (!strcmp(a, "--port") && next) {
            cfg->web_port = atoi(next);
            i++;
        } else if (!strcmp(a, "--cli")) {
            cfg->use_cli = 1;
        } else if (!strcmp(a, "--quiet")) {
            cfg->verbose = 0;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            engine_print_usage(argv[0]);
            return 1;
        } else if (!strcmp(a, "--input") && next) {
            snprintf(cfg->input_dev, sizeof(cfg->input_dev), "%s", next);
            i++;
        } else if (!strcmp(a, "--output") && next) {
            snprintf(cfg->output_dev, sizeof(cfg->output_dev), "%s", next);
            i++;
        } else if (!strcmp(a, "--rate") && next) {
            cfg->rate = (unsigned int)atoi(next);
            i++;
        } else if (!strcmp(a, "--channels") && next) {
            cfg->channels = (unsigned int)atoi(next);
            i++;
        } else if (!strcmp(a, "--period") && next) {
            cfg->period_frames = (unsigned int)atoi(next);
            i++;
        } else if (!strcmp(a, "--buffer") && next) {
            cfg->buffer_frames = (unsigned int)atoi(next);
            i++;
        } else if (!strcmp(a, "--mic-gain") && next) {
            cfg->mic_gain = strtof(next, NULL);
            i++;
        } else if (!strcmp(a, "--amp-gain") && next) {
            cfg->amp_gain = strtof(next, NULL);
            i++;
        } else {
            fprintf(stderr, "Unknown option: %s\n", a);
            engine_print_usage(argv[0]);
            return -1;
        }
    }

    if (cfg->rate < 8000 || cfg->channels < 1 || cfg->period_frames < 32) {
        fprintf(stderr, "Invalid rate/channels/period\n");
        return -1;
    }
    if (cfg->web_port < 1 || cfg->web_port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return -1;
    }
    if (cfg->buffer_frames < cfg->period_frames * 2) {
        cfg->buffer_frames = cfg->period_frames * 2;
    }
    if (!cfg->list_only) {
        try_autofill_play3(cfg);
        if (!strcmp(cfg->output_dev, "default") && strcmp(cfg->input_dev, "default") != 0) {
            snprintf(cfg->output_dev, sizeof(cfg->output_dev), "%s", cfg->input_dev);
        }
    }
    return 0;
}

static int skip_hint_name(const char* name) {
    if (!name || !name[0]) {
        return 1;
    }
    /* Keep real cards; skip abstract plugins that clutter the device list. */
    if (!strcmp(name, "null") || !strcmp(name, "oss") || !strcmp(name, "jack") ||
        !strcmp(name, "speex") || !strcmp(name, "upmix") || !strcmp(name, "vdownmix") ||
        !strcmp(name, "lavrate") || !strcmp(name, "samplerate") || !strcmp(name, "a52") ||
        !strcmp(name, "dmix") || !strcmp(name, "dsnoop")) {
        return 1;
    }
    return 0;
}

static int hint_matches_stream(const char* ioid, int want_capture) {
    if (!ioid) {
        return 1; /* both */
    }
    if (want_capture) {
        return strcmp(ioid, "Output") != 0;
    }
    return strcmp(ioid, "Input") != 0;
}

static int enumerate_stream(AlsaDeviceList* out, int want_capture) {
    memset(out, 0, sizeof(*out));
    void** hints = NULL;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0 || !hints) {
        set_error("Failed to query ALSA PCM device hints");
        return -1;
    }

    int capacity = 0;
    for (void** h = hints; *h; h++) {
        char* name = snd_device_name_get_hint(*h, "NAME");
        char* desc = snd_device_name_get_hint(*h, "DESC");
        char* ioid = snd_device_name_get_hint(*h, "IOID");
        if (name && !skip_hint_name(name) && hint_matches_stream(ioid, want_capture)) {
            if (out->count >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                AlsaDeviceInfo* grown =
                    (AlsaDeviceInfo*)realloc(out->items, (size_t)capacity * sizeof(AlsaDeviceInfo));
                if (!grown) {
                    free(name);
                    free(desc);
                    free(ioid);
                    alsa_device_list_free(out);
                    snd_device_name_free_hint(hints);
                    set_error("Out of memory enumerating ALSA devices");
                    return -1;
                }
                out->items = grown;
            }
            AlsaDeviceInfo* item = &out->items[out->count++];
            snprintf(item->id, sizeof(item->id), "%s", name);
            if (desc && desc[0]) {
                /* First line of DESC is usually the card product name. */
                char one_line[200];
                snprintf(one_line, sizeof(one_line), "%s", desc);
                char* nl = strchr(one_line, '\n');
                if (nl) {
                    *nl = '\0';
                }
                snprintf(item->label, sizeof(item->label), "%s  [%s]", one_line, name);
            } else {
                snprintf(item->label, sizeof(item->label), "%s", name);
            }
        }
        free(name);
        free(desc);
        free(ioid);
    }
    snd_device_name_free_hint(hints);
    return 0;
}

int alsa_enumerate_capture(AlsaDeviceList* out) {
    return enumerate_stream(out, 1);
}

int alsa_enumerate_playback(AlsaDeviceList* out) {
    return enumerate_stream(out, 0);
}

void alsa_device_list_free(AlsaDeviceList* list) {
    if (!list) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int alsa_list_devices(void) {
    AlsaDeviceList capture = {};
    AlsaDeviceList playback = {};
    if (alsa_enumerate_capture(&capture) < 0 || alsa_enumerate_playback(&playback) < 0) {
        alsa_device_list_free(&capture);
        alsa_device_list_free(&playback);
        return -1;
    }
    printf("Capture devices:\n");
    for (int i = 0; i < capture.count; i++) {
        printf("  %s\n", capture.items[i].label);
    }
    printf("Playback devices:\n");
    for (int i = 0; i < playback.count; i++) {
        printf("  %s\n", playback.items[i].label);
    }
    alsa_device_list_free(&capture);
    alsa_device_list_free(&playback);
    return 0;
}

static int configure_stream(snd_pcm_t* pcm,
                            snd_pcm_stream_t stream,
                            const EngineConfig* cfg,
                            unsigned int* rate_out,
                            unsigned int* channels_out,
                            unsigned int* period_out,
                            unsigned int* buffer_out) {
    const char* which = stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback";
    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_hw_params_alloca(&hw);
    int err = snd_pcm_hw_params_any(pcm, hw);
    if (err < 0) {
        set_error("%s hw_params_any: %s", which, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        set_error("%s set_access interleaved: %s", which, snd_strerror(err));
        return err;
    }
    err = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        set_error("%s does not support S16_LE: %s", which, snd_strerror(err));
        return err;
    }

    /* Exact rate/channels — do not silently renegotiate Milestone 1 format. */
    err = snd_pcm_hw_params_set_rate(pcm, hw, cfg->rate, 0);
    if (err < 0) {
        set_error("%s does not support %u Hz: %s", which, cfg->rate, snd_strerror(err));
        return err;
    }
    err = snd_pcm_hw_params_set_channels(pcm, hw, cfg->channels);
    if (err < 0) {
        set_error("%s does not support %u channels: %s", which, cfg->channels, snd_strerror(err));
        return err;
    }

    snd_pcm_uframes_t period = cfg->period_frames;
    err = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
    if (err < 0) {
        set_error("%s set_period: %s", which, snd_strerror(err));
        return err;
    }

    snd_pcm_uframes_t buffer = cfg->buffer_frames;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
    if (err < 0) {
        set_error("%s set_buffer: %s", which, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) {
        set_error("%s hw_params apply failed: %s", which, snd_strerror(err));
        return err;
    }

    *rate_out = cfg->rate;
    *channels_out = cfg->channels;
    *period_out = (unsigned int)period;
    *buffer_out = (unsigned int)buffer;
    return 0;
}

static int xrun_recover(snd_pcm_t* pcm, int err, const char* which) {
    fprintf(stderr, "ALSA %s xrun/error: %s — recovering\n", which, snd_strerror(err));
    err = snd_pcm_recover(pcm, err, 1);
    if (err < 0) {
        set_error("recover failed on %s: %s", which, snd_strerror(err));
        return err;
    }
    return 0;
}

AlsaDuplex* alsa_open(const EngineConfig* cfg) {
    g_last_error[0] = '\0';
    AlsaDuplex* io = (AlsaDuplex*)calloc(1, sizeof(AlsaDuplex));
    if (!io) {
        set_error("Out of memory opening ALSA duplex");
        return NULL;
    }
    snprintf(io->input_name, sizeof(io->input_name), "%s", cfg->input_dev);
    snprintf(io->output_name, sizeof(io->output_name), "%s", cfg->output_dev);

    int err = snd_pcm_open(&io->capture, cfg->input_dev, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        set_error("Cannot open capture '%s': %s", cfg->input_dev, snd_strerror(err));
        free(io);
        return NULL;
    }
    err = snd_pcm_open(&io->playback, cfg->output_dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        set_error("Cannot open playback '%s': %s", cfg->output_dev, snd_strerror(err));
        snd_pcm_close(io->capture);
        free(io);
        return NULL;
    }

    unsigned int c_rate = 0, c_ch = 0, c_period = 0, c_buffer = 0;
    unsigned int p_rate = 0, p_ch = 0, p_period = 0, p_buffer = 0;
    if (configure_stream(io->capture, SND_PCM_STREAM_CAPTURE, cfg, &c_rate, &c_ch, &c_period, &c_buffer) < 0 ||
        configure_stream(io->playback, SND_PCM_STREAM_PLAYBACK, cfg, &p_rate, &p_ch, &p_period, &p_buffer) < 0) {
        alsa_close(io);
        return NULL;
    }

    if (c_rate != cfg->rate || c_ch != cfg->channels || p_rate != cfg->rate || p_ch != cfg->channels) {
        set_error("Device renegotiated format (wanted %u Hz %u ch)", cfg->rate, cfg->channels);
        alsa_close(io);
        return NULL;
    }

    io->rate = c_rate;
    io->channels = c_ch;
    io->period_frames = c_period < p_period ? c_period : p_period;
    io->buffer_frames = c_buffer > p_buffer ? c_buffer : p_buffer;

    size_t silence_samples = (size_t)io->period_frames * io->channels;
    int16_t* silence = (int16_t*)calloc(silence_samples, sizeof(int16_t));
    if (silence) {
        snd_pcm_writei(io->playback, silence, io->period_frames);
        free(silence);
    }

    snd_pcm_prepare(io->capture);
    snd_pcm_prepare(io->playback);
    return io;
}

void alsa_close(AlsaDuplex* io) {
    if (!io) {
        return;
    }
    if (io->capture) {
        snd_pcm_drop(io->capture);
        snd_pcm_close(io->capture);
    }
    if (io->playback) {
        snd_pcm_drop(io->playback);
        snd_pcm_close(io->playback);
    }
    free(io);
}

void alsa_print_info(const AlsaDuplex* io) {
    if (!io) {
        return;
    }
    fprintf(stderr,
            "ALSA duplex ready\n"
            "  capture : %s\n"
            "  playback: %s\n"
            "  format  : S16_LE  %u Hz  %u ch (interleaved, no downmix)\n"
            "  period  : %u frames\n"
            "  buffer  : %u frames\n",
            io->input_name,
            io->output_name,
            io->rate,
            io->channels,
            io->period_frames,
            io->buffer_frames);
}

int alsa_read(AlsaDuplex* io, int16_t* interleaved, unsigned int frame_count) {
    while (1) {
        snd_pcm_sframes_t n = snd_pcm_readi(io->capture, interleaved, frame_count);
        if (n == (snd_pcm_sframes_t)frame_count) {
            return 0;
        }
        if (n < 0) {
            if (xrun_recover(io->capture, (int)n, "capture") < 0) {
                return -1;
            }
            continue;
        }
        size_t got = (size_t)n * io->channels;
        size_t want = (size_t)frame_count * io->channels;
        memset(interleaved + got, 0, (want - got) * sizeof(int16_t));
        return 0;
    }
}

int alsa_write(AlsaDuplex* io, const int16_t* interleaved, unsigned int frame_count) {
    const int16_t* ptr = interleaved;
    unsigned int left = frame_count;
    while (left > 0) {
        snd_pcm_sframes_t n = snd_pcm_writei(io->playback, ptr, left);
        if (n == (snd_pcm_sframes_t)left) {
            return 0;
        }
        if (n < 0) {
            if (xrun_recover(io->playback, (int)n, "playback") < 0) {
                return -1;
            }
            continue;
        }
        ptr += (size_t)n * io->channels;
        left -= (unsigned int)n;
    }
    return 0;
}

unsigned int alsa_period_frames(const AlsaDuplex* io) {
    return io ? io->period_frames : 0;
}

unsigned int alsa_channels(const AlsaDuplex* io) {
    return io ? io->channels : 0;
}

unsigned int alsa_rate(const AlsaDuplex* io) {
    return io ? io->rate : 0;
}
