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

### Voices

The top of the page is the voice picker: tap **Clean / Bypass**, **DJ R3X**, **DJ R3X (vocal model)**, **TIE Pilot**, **Stormtrooper** or **Droid** and the whole character loads at once. The active voice is shown in the box above the buttons. No tuning is needed. Switching while audio is running crossfades over about 10 ms (one audio period out, one in), clears the old voice's echo tail, and doesn't restart the audio engine.

**More voices** has the extras: Dark Mechanical, Quirky Droid, the original (classic) Stormtrooper / TIE Pilot / Droid, Radio and Villain. Presets you save yourself show up as voice buttons too.

Adding a new character later (e.g. Chopper) is one row in `kPresets` plus one branch in `fx_apply_preset()` in `linux/dsp.cpp`.

### Personal tuning

A voice defines the **character**; a **tuning** adapts that character to the person wearing the costume. Under the voice buttons:

- **Tuning** — pick **Default** (the voice exactly as designed) or one of your saved tunings, e.g. **Frank**. Each voice remembers which tuning you last used, so tapping **DJ R3X** brings back your R3X tuning (also at boot).
- **Fine tune** — opens nine broad controls. Centre is always "as designed"; double-tap a slider to re-centre it.

| Control | What it moves |
|---------|---------------|
| Pitch | Pitch in semitones (with the vocal model, formants stay put) |
| Character | Vocal-tract size and resonance — smaller/brighter vs larger/darker head |
| Body | Low end: high-pass corner, low/mid resonances, low shelf |
| Presence | Upper mids/top: presence peak, low-pass corner, upper resonances, high shelf, helmet top |
| Mechanical | Ring modulation, metallic comb, resonance sharpness |
| Helmet / Cavity | Helmet band-limit and short reflections (adds a helmet if the voice has none) |
| Saturation | The voice's grit stage (saturation or clipping), or a tanh stage faded in |
| Wet / Dry | How much of the character processing is mixed in |
| Output | Output level before the limiter |

**Save to …** updates the selected tuning, **Save as new…** creates one (e.g. your name), **Undo changes** reloads it, **Delete tuning** removes it. Tunings never change the voice preset itself. They live in `~/.config/uvc-voicechanger/tunings.ini`. Tuning changes glide over ~20 ms, so moving sliders while talking doesn't click.

### Advanced: DSP tuning

The collapsed **Advanced** section holds every DSP control, preset save/restore, and an **A/B compare** switch: **Bypass** (dry), **Classic DSP** (the original chain only, all character stages off) and **Character DSP** (everything). The A/B setting is not saved.

Actual signal chain per channel (L and R are processed separately):

```
input gain
→ high-pass ×n → low-pass ×n → presence peak        (classic)
→ ring modulator                                    (classic)
→ metal / cavity comb (optional feedback damping)   (classic, improved)
→ volume
→ pitch shift                                       (classic)
  [vocal model] in parallel: vocal-tract analysis → pitch shift of the
  excitation only → resynthesis with a reshaped tract, blended by Mix
→ [character stages]
    vocal character → compressor → split:
      dry ──────────────────────────────────────┐
      wet: resonators → saturation → helmet ────┴→ wet mix
    → final shelf EQ
→ soft clip                                         (classic)
→ output level
→ soft limiter (safety)
→ 16-bit out
```

| Stage | What it does |
|-------|--------------|
| Pitch | Shifts up/down in semitones (low-latency delay-line shifter; big shifts sound slightly warbly). On its own it moves the formants too, which is what makes a shifted voice sound "sped up" |
| Vocal model | Tracks your vocal tract (LPC, updated every 2.7 ms), pitch-shifts only the buzz underneath, and rebuilds the voice through a reshaped tract. **Tract size** moves all your formants independently of pitch (>1 smaller/brighter head, <1 bigger); **Resonance** makes them sharper/hollower or softer; **Mix** blends with the classic pitch path. Resonances follow your vowels because they come from your own speech |
| High-pass / Low-pass | Cut bass / treble; "Steepness" cascades filters like the ESP |
| Presence peak | Boost or cut a frequency band (nasal / tinny character) |
| Ring modulator | Robot / Dalek buzz; "Mix" blends it with the dry voice |
| Metal / cavity / echo | Short delay = metallic cavity; long = echo. Feedback 0 = no tail; Damping darkens the ringing. Feedback is capped below 1, so it can't run away |
| Clipping | Soft distortion / grit (original ESP curve) |
| Vocal character | Three vocal-tract-style resonances + a nasal peak whose frequencies scale together ("Size scale"), plus a spectral tilt. **This is a resonant-EQ approximation, not a true formant shifter**: it colours the voice as bigger/smaller/nasal but doesn't move your own formants |
| Compressor | Soft-knee peak compressor (threshold, ratio, attack, release, makeup) — evens out level so the character holds on quiet words |
| Resonators | Four tunable peak resonances. Helmet starting points 500/900/1800/3000 Hz, droid 700/1200/2200/3500 Hz |
| Saturation | tanh warmth/grit, separate from Clipping; unity gain on quiet signals, Asymmetry adds even harmonics |
| Helmet / comms | Band limit + very short damped reflections + optional amplitude modulation (engine hum) |
| Wet mix | Blends the character path with the (compressed) dry voice — keeps intelligibility |
| Final EQ | Low/high shelves |
| Limiter | Zero-latency soft limiter; output never exceeds the ceiling |

Moving any slider switches to **Custom (from …)**; changes apply immediately without clicks. Saved presets from older versions load unchanged (the new stages default to off).

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
6. Tap a voice (e.g. **DJ R3X**) at the top of the page.

### Start at boot (one-time setup)

```bash
cd ~/UVC_VoiceChanger/linux
sudo sh install-service.sh
```

After this, `uvc_pass` and the web page run at every boot. Everything else is on the page:

- **Start audio automatically** — starts audio on the devices you picked when the Pi boots, and retries if the USB audio shows up late or gets unplugged and replugged. Pressing **Stop audio** pauses retries until you press **Start audio** again.
- **Load this voice at startup** — tick it while a voice is selected to make that voice load at boot. The line underneath shows the current choice.

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
