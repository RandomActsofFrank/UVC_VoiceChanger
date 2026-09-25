# UVC VoiceChanger

ESP32/ESP32-S3 cosplay voice changer by [s60sc](https://github.com/s60sc/ESP32_VoiceChanger), plus a Linux port for Raspberry Pi 4 + USB audio.

| Path | Contents |
|------|----------|
| [`ESP/`](ESP/) | Original Arduino firmware (unchanged reference) |
| [`linux/`](linux/) | Pi audio engine — Milestone 1 ALSA stereo pass-through + device GUI |

**All setup below is meant to be run on the Raspberry Pi itself** (SSH or local terminal), not from a Mac/PC.

Supported boards: **Pi Zero 2 W** (headless, recommended: Raspberry Pi OS Lite 64-bit) and **Pi 4** (desktop GUI optional).

**Pi Zero 2 W notes**

- It has one USB data port (the micro-USB labelled **USB**, not **PWR**). Use a micro-USB OTG adapter or a small OTG hub to connect the Play! 3.
- The Play! 3 is both capture and playback, so one USB device covers input and output.
- 512 MB RAM: skip the desktop and GTK; build with `make NO_GUI=1` and run `--cli`.
- Use a solid 5 V / 2.5 A supply; USB audio dropouts on the Zero are often power-related.

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

### 1. Clone from GitHub (on the Pi)

```bash
sudo apt update
sudo apt install -y git
cd ~
git clone https://github.com/RandomActsofFrank/UVC_VoiceChanger.git
cd UVC_VoiceChanger
```

To refresh later:

```bash
cd ~/UVC_VoiceChanger
git pull
```

### 2. Install build dependencies

Pi Zero 2 W (headless):

```bash
sudo apt install -y build-essential libasound2-dev pkg-config
```

Pi 4 with desktop (adds the GTK GUI):

```bash
sudo apt install -y build-essential libasound2-dev libgtk-3-dev pkg-config
```

### 3. Build

```bash
cd ~/UVC_VoiceChanger/linux
make NO_GUI=1   # Pi Zero 2 W
# or
make            # Pi 4; includes the GUI if libgtk-3-dev is installed
```

### 4. Run (GUI, Pi 4 only)

Needs a GTK build and a display (desktop session, or `DISPLAY` set over SSH with X11 forwarding / VNC).

```bash
cd ~/UVC_VoiceChanger/linux
./uvc_pass
# or
./uvc_pass --gui
```

1. Choose **INPUT DEVICE** (USB capture / Play! 3).
2. Choose **OUTPUT DEVICE** (USB speakers).
3. Click **Start Audio**.
4. Click **Stop Audio** before changing devices.
5. **Refresh devices** after plugging USB gear.

### 5. Run (CLI / headless, Pi Zero 2 W)

```bash
cd ~/UVC_VoiceChanger/linux
./uvc_pass --list
./uvc_pass --cli --input plughw:1,0 --output plughw:1,0
```

With only the Play! 3 attached, input and output are usually the same card (use the card number `--list` shows). If you get XRUNs on the Zero, try a larger buffer: `--period 512 --buffer 2048`.

### Later milestones (not started)

Port ESP biquads/filters; use the historical Pi Vader project as a sound reference only. No DSP in this milestone.

## License

AGPL-3.0 (same as the ESP32 original).
