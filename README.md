# UVC VoiceChanger

ESP32/ESP32-S3 cosplay voice changer by [s60sc](https://github.com/s60sc/ESP32_VoiceChanger), plus a Linux port for Raspberry Pi 4 + USB audio.

| Path | Contents |
|------|----------|
| [`ESP/`](ESP/) | Original Arduino firmware (unchanged reference) |
| [`linux/`](linux/) | Pi audio engine — Milestone 1 ALSA stereo pass-through + device GUI |

## Milestone 1 — ALSA stereo pass-through + device selection

```
USB capture (e.g. Play! 3 / DJI Mic 2)
        ↓
      ALSA
        ↓
 stereo PCM (L→L, R→R)  — direct copy, no DSP
        ↓
      ALSA
        ↓
USB playback (speakers / headphones / amp)
```

- Strict format: **48000 Hz**, **S16_LE**, **2 channels**
- No mono downmix, no channel duplication, no DSP
- GTK window to pick input/output from live ALSA enumeration
- Start / Stop; change devices only while stopped
- XRUN recovery retained
- CLI mode still available (`--cli`)

### Build on Raspberry Pi OS 64-bit

```bash
sudo apt update
sudo apt install build-essential libasound2-dev libgtk-3-dev pkg-config
cd linux
make
```

### Run (GUI)

```bash
cd linux
./uvc_pass
# or
./uvc_pass --gui
```

1. Choose **INPUT DEVICE** (USB capture / Play! 3).
2. Choose **OUTPUT DEVICE** (USB speakers).
3. Click **Start Audio**.
4. Click **Stop Audio** before changing devices.
5. **Refresh devices** after plugging USB gear.

### Run (CLI / headless)

```bash
./uvc_pass --list
./uvc_pass --cli --input plughw:1,0 --output plughw:2,0
```

### Later milestones (not started)

Port ESP biquads/filters; use the historical Pi Vader project as a sound reference only. No DSP in this milestone.

## License

AGPL-3.0 (same as the ESP32 original).
