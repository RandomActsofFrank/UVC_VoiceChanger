#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char input_dev[128];
    char output_dev[128];
    unsigned int rate;
    unsigned int channels; /* Milestone 1: must be 2 */
    unsigned int period_frames;
    unsigned int buffer_frames;
    float mic_gain;
    float amp_gain;
    int list_only;
    int verbose;
    int use_cli;
    int web_port;
    char preset[32];        /* voice preset id applied at startup; empty = saved choice */
    char presets_path[256]; /* saved presets file; empty = default location */
} EngineConfig;

typedef struct {
    char id[128];    /* ALSA PCM name passed to snd_pcm_open */
    char label[256]; /* human-readable label for the web page */
} AlsaDeviceInfo;

typedef struct {
    AlsaDeviceInfo* items;
    int count;
} AlsaDeviceList;

typedef struct AlsaDuplex AlsaDuplex;

void engine_config_defaults(EngineConfig* cfg);
int engine_parse_args(int argc, char** argv, EngineConfig* cfg);
void engine_print_usage(const char* argv0);

int alsa_list_devices(void);
/* One entry per physical device (plughw:CARD=<id>,DEV=<n>). */
int alsa_enumerate_capture(AlsaDeviceList* out);
int alsa_enumerate_playback(AlsaDeviceList* out);
/* Every ALSA PCM name, for troubleshooting. */
int alsa_enumerate_all_capture(AlsaDeviceList* out);
int alsa_enumerate_all_playback(AlsaDeviceList* out);
void alsa_device_list_free(AlsaDeviceList* list);

AlsaDuplex* alsa_open(const EngineConfig* cfg);
void alsa_close(AlsaDuplex* io);
int alsa_read(AlsaDuplex* io, int16_t* interleaved, unsigned int frame_count);
int alsa_write(AlsaDuplex* io, const int16_t* interleaved, unsigned int frame_count);
void alsa_print_info(const AlsaDuplex* io);
const char* alsa_last_error(void);

unsigned int alsa_period_frames(const AlsaDuplex* io);
unsigned int alsa_channels(const AlsaDuplex* io);
unsigned int alsa_rate(const AlsaDuplex* io);

#ifdef __cplusplus
}
#endif
