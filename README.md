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
| Metal / echo | Short delay = metallic tin-can tone; long delay = echo. Feedback 0 = no tail; raise it for ringing / repeats |
| Clipping | Soft distortion / grit |

Presets: **Clean**, **DJ R3X**, **Droid**, **Stormtrooper**, **TIE Pilot**, **Radio**, **Villain**. Picking a preset loads its settings; moving any slider switches to **Custom (from …)**. Changes apply immediately, even while audio is running.

### Saving presets

- **Save to …** overwrites the preset you started from with the current sliders.
- **Save as new…** asks for a name. Using an existing preset's name overwrites that preset (after confirming).
- **Restore defaults** puts a built-in preset back to its original settings. For presets you created, this button is **Delete preset**.

Built-in presets you've changed show **(edited)**. Saved presets live in `~/.config/uvc-voicechanger/presets.ini` on the Pi (change with `--presets FILE`) and survive restarts. `--preset ID` works with saved presets too; a new preset's ID is its name in lowercase with dashes, e.g. "My R3X" → `my-r3x`.

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

### Start at boot (one-time setup)

```bash
cd ~/UVC_VoiceChanger/linux
sudo sh install-service.sh
```

After this, `uvc_pass` and the web page run at every boot. Everything else is on the page:

- **Start audio automatically** — starts audio on the devices you picked when the Pi boots, and retries if the USB audio shows up late or gets unplugged and replugged. Pressing **Stop audio** pauses retries until you press **Start audio** again.
- **Load this preset at startup** — tick it while a preset is selected to make that preset load at boot. The line underneath shows the current choice.

The page remembers the devices from the last successful **Start audio**. Settings live in `~/.config/uvc-voicechanger/settings.ini`.

With the service installed, stop it before running `./uvc_pass` by hand, and restart it after rebuilding:

```bash
sudo systemctl stop uvc-voicechanger      # before running ./uvc_pass manually
sudo systemctl restart uvc-voicechanger   # after git pull + make
journalctl -u uvc-voicechanger -f         # view its log
sudo sh install-service.sh --remove       # uninstall
```

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
