#pragma once

#include "alsa_io.h"

#include <signal.h>

/* Serve the device-selection web page until *keep_running becomes 0. */
int run_web(const EngineConfig* defaults, volatile sig_atomic_t* keep_running);
