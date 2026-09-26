#pragma once

#include "alsa_io.h"
#include "ptt.h"

#include <signal.h>

/* Serve the device-selection web page until *keep_running becomes 0.
   ptt is shown/configured on the page and gates the engine's output. */
int run_web(const EngineConfig* defaults, volatile sig_atomic_t* keep_running, PttMonitor* ptt);
