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
    unsigned int channels; /* default 2 — stereo pass-through, no downmix */
    unsigned int period_frames;
    unsigned int buffer_frames;
    float mic_gain;  /* linear amplitude multiplier on capture */
    float amp_gain;  /* linear amplitude multiplier before playback */
    int list_only;
    int verbose;
} EngineConfig;

typedef struct AlsaDuplex AlsaDuplex;

void engine_config_defaults(EngineConfig* cfg);
int engine_parse_args(int argc, char** argv, EngineConfig* cfg);
void engine_print_usage(const char* argv0);

int alsa_list_devices(void);
AlsaDuplex* alsa_open(const EngineConfig* cfg);
void alsa_close(AlsaDuplex* io);
int alsa_read(AlsaDuplex* io, int16_t* interleaved, unsigned int frame_count);
int alsa_write(AlsaDuplex* io, const int16_t* interleaved, unsigned int frame_count);
void alsa_print_info(const AlsaDuplex* io);

unsigned int alsa_period_frames(const AlsaDuplex* io);
unsigned int alsa_channels(const AlsaDuplex* io);
unsigned int alsa_rate(const AlsaDuplex* io);

#ifdef __cplusplus
}
#endif
