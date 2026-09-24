# UVC VoiceChanger

ESP32/ESP32-S3 cosplay voice changer by [s60sc](https://github.com/s60sc/ESP32_VoiceChanger), plus a Linux port for Raspberry Pi + Sound Blaster Play! 3.

| Path | Contents |
|------|----------|
| [`ESP/`](ESP/) | Original Arduino firmware (unchanged reference) |
| [`linux/`](linux/) | Pi audio engine (Milestone 1: ALSA stereo pass-through) |

## Milestone 1 — ALSA stereo pass-through

Goal: prove USB audio in and out on a Pi 4 before porting the existing DSP.

```
Play! 3 mic  →  ALSA capture  →  gain  →  ALSA playback  →  Play! 3 headphones
```

- Default **48 kHz**, **2 channels** (stereo). No mono downmix.
- Configurable rate, channels, period, buffer, devices, gains.
- ALSA XRUN recovery and level diagnostics.
- **No** DSP, web UI, GUI, or pitch shift yet.

### Build on Raspberry Pi OS 64-bit

```bash
sudo apt update
sudo apt install build-essential libasound2-dev
cd linux
make
```

### Run

```bash
# See devices (Play! 3 candidates are marked)
./uvc_pass --list

# Auto-pick Play! 3 if the name matches, else use default
./uvc_pass

# Explicit devices
./uvc_pass --input plughw:1,0 --output plughw:1,0 --rate 48000 --channels 2

# Gains (linear)
./uvc_pass --mic-gain 1.5 --amp-gain 0.8
```

Ctrl+C stops. Peak/RMS levels print about once per second.

### Later milestones (not started)

Port `ESP/Biquad.*` and `ESP/Filters.cpp` with the same processing order as the firmware. Leave `smbPitchShift.cpp` disabled until pass-through and DSP are solid.

## License

AGPL-3.0 (same as the ESP32 original). Biquad and pitch-shift third-party notices remain in `ESP/`.
