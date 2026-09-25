# UVC VoiceChanger

ESP32/ESP32-S3 cosplay voice changer by [s60sc](https://github.com/s60sc/ESP32_VoiceChanger), plus a Linux port for Raspberry Pi + USB audio.

| Path | Contents |
|------|----------|
| [`ESP/`](ESP/) | Original Arduino firmware (unchanged reference) |
| [`linux/`](linux/) | Pi audio engine — ALSA stereo audio, voice effects, web config |

**All setup below is meant to be run on the Raspberry Pi itself** (over SSH), not from a Mac/PC.

Target board: **Pi Zero 2 W** running **Raspberry Pi OS Lite 64-bit**, no display. Configuration is done from a web page on a phone or computer on the same network. A Pi 4 works the same way.

**Pi Zero 2 W notes**

- It has one USB data port (the micro-USB labelled **USB**, not **PWR**). Use a micro-USB OTG adapter or a small OTG hub to connect the Play! 3.
- The Play! 3 is both capture and playback, so one USB device covers input and output.
- Use a solid 5 V / 2.5 A supply; USB audio dropouts on the Zero are often power-related.

## Audio path

```
USB capture (e.g. Play! 3 / DJI Mic 2)
        ↓
      ALSA
        ↓
 voice effects, each channel separately (L→L, R→R)
        ↓
      ALSA
        ↓
USB playback (speakers / headphones / amp)
```

- Strict format: **48000 Hz**, **S16_LE**, **2 channels**; no mono downmix
- Web page to pick input/output devices, Start / Stop, and adjust the voice live
- XRUN recovery retained
- CLI mode still available (`--cli`)

### Voice effects

Same building blocks and order as the ESP32 firmware (`ESP/Filters.cpp`):
filters → ring modulator → metal/echo → volume → pitch → clipping.

| Effect | What it does |
|--------|--------------|
| Pitch | Shifts up/down in semitones (low-latency delay-line shifter; big shifts sound slightly warbly) |
| High-pass / Low-pass | Cut bass / treble; "Steepness" cascades filters like the ESP |
| Presence peak | Boost or cut a frequency band (nasal / tinny character) |
| Ring modulator | Robot / Dalek buzz; "Mix" blends it with the dry voice |
| Metal / echo | Short delay = metallic tin-can resonance; long delay = echo |
| Clipping | Soft distortion / grit |

Presets: **Clean**, **DJ R3X**, **Droid**, **Stormtrooper**, **TIE Pilot**, **Radio**, **Villain**. Picking a preset loads its settings; moving any slider switches to **Custom**. Changes apply immediately, even while audio is running.

Settings are not saved yet — restarting `uvc_pass` goes back to the startup preset (`--preset r3x` to start as DJ R3X).

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

```bash
sudo apt install -y build-essential libasound2-dev
```

### 3. Build

```bash
cd ~/UVC_VoiceChanger/linux
make
```

### 4. Run (web page)

```bash
cd ~/UVC_VoiceChanger/linux
./uvc_pass
```

It prints the address to open, e.g. `http://voicepi.local:8080/`. Open that from a phone or computer on the same Wi-Fi.

1. Choose **Input device** (USB capture / Play! 3).
2. Choose **Output device** (USB speakers).
3. Tap **Start audio**.
4. Tap **Stop audio** before changing devices.
5. **Refresh devices** after plugging USB gear.
6. Under **Voice**, pick a preset (e.g. **DJ R3X**) and fine-tune with the sliders.

Use `--port N` to change the port. The page has no password, so only run it on a network you trust. Ctrl+C in the SSH session quits it and stops audio.

### 5. Run (CLI, no web page)

```bash
cd ~/UVC_VoiceChanger/linux
./uvc_pass --list
./uvc_pass --cli --preset r3x --input plughw:CARD=S3,DEV=0 --output plughw:CARD=S3,DEV=0
```

Use the device names `--list` prints. If you get XRUNs on the Zero, try a larger buffer: `--period 512 --buffer 2048`.

## License

AGPL-3.0 (same as the ESP32 original).
