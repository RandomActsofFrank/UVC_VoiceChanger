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
 push-to-talk output gate (optional; RC receiver button)
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

The top of the page is the voice picker: tap **Clean / Bypass**, **DJ R3X**, **TIE Pilot**, **Stormtrooper** or **Droid** (R3X, TIE Pilot and Stormtrooper also come as a **(vocal model)** version) and the whole character loads at once. The active voice is shown in the box above the buttons. No tuning is needed. Switching while audio is running crossfades over about 10 ms (one audio period out, one in), clears the old voice's echo tail, and doesn't restart the audio engine.

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

### Push-to-talk (RC transmitter button)

A button on an RC transmitter becomes the microphone's push-to-talk: the receiver's PWM channel goes to one Pi GPIO pin. While the button is held the processed voice plays through the speakers; when it's released the speakers are muted. The voice processing never stops (so there's no warm-up when you press), ALSA is never stopped or reopened, and mute/unmute is a 10 ms fade, so there are no clicks.

**Fail-safe:** the speakers are on only while valid pulses keep arriving *and* they're in the ON range. No pulse for longer than the timeout (receiver unpowered or unplugged, wire broken, GPIO unavailable, monitor stalled), or a pulse outside the valid range, mutes immediately. When the signal comes back, it needs three good pulses in a row and stays muted unless the button is actually held.

PTT works whether or not the web page is open, and in `--cli` mode. It's off by default; with it off, audio is exactly as before.

**Settings** (Push-to-talk section of the page, stored in `settings.ini`):

| Setting | Default | Meaning |
|---------|---------|---------|
| Enable push-to-talk | off | Off = speakers always on |
| GPIO | 17 (header pin 11) | BCM GPIO number of the receiver signal |
| ON threshold | 1700 µs | Pulses at or over this unmute |
| OFF threshold | 1300 µs | Pulses at or under this mute; between ON and OFF keeps the current state (hysteresis) |
| Signal timeout | 100 ms | No valid pulse for this long = signal lost = muted (RC frames are ~20 ms) |
| Valid pulse min / max | 700 / 2300 µs | Anything outside is treated as a bad signal = muted |

Set ON *below* OFF if your button shortens the pulse instead of lengthening it. To pick thresholds, open **PTT settings**, watch the **PWM** reading while you hold and release the button, and put ON and OFF a bit inside those two readings. The status box under the voice shows **PTT: READY** (released), **PTT: TALKING** (held) or **PTT: SIGNAL LOST**. `ptt_chip=` in `settings.ini` overrides the GPIO chip (auto-detected on Pi Zero 2 W, 3, 4 and 5).

GPIO access uses the kernel's GPIO character device (`/dev/gpiochip*`, edge events with kernel timestamps) — no extra packages. The pin gets the internal pull-down, so a disconnected wire reads as "no signal". The service runs with the `gpio` group; if the page shows a permission error, run `sudo usermod -aG gpio $USER`, reboot, and re-run `sudo sh install-service.sh`.

**Wiring and electrical safety — read before connecting:**

- **Raspberry Pi GPIO is 3.3 V only and NOT 5 V tolerant.** A 5 V signal on a GPIO pin can permanently damage the Pi.
- Many 2.4 GHz receivers output 3.3 V signal pulses even when powered from 5 V, but **some output 5 V**. The signal level of your receiver is unknown until measured: check the signal pin with a multimeter/oscilloscope (a servo signal on a multimeter reads roughly 0.3–0.5 V DC average; a scope shows the actual pulse height) or check the receiver's documentation.
- If the signal is 5 V, or you're not sure, put a level shifter or a resistor divider in between, e.g. **10 kΩ in series from the receiver signal to the GPIO pin and 20 kΩ from the GPIO pin to ground** (5 V → 3.3 V). Even with a 3.3 V receiver, a **1 kΩ series resistor** is cheap protection.
- Connect **receiver ground to a Pi ground pin** (e.g. header pin 9 or 14). Without a common ground the Pi can't read the signal.
- Power the receiver from its own BEC/battery or the Pi's 5 V pin (receivers draw roughly 30–100 mA) — **never** from the Pi's 3.3 V pin.
- Default pin: **GPIO17 = header pin 11**, ground on pin 9. Avoid GPIO 2/3 (I²C, fixed pull-ups), 14/15 (serial console) and 18–21 (I²S audio HATs).

**Receiver failsafe — important:** when the transmitter is switched off or out of range, many receivers keep sending pulses at a *failsafe* position, and some hold the *last* position. The Pi can't tell that apart from a real signal, so if the button was held, the speakers would stay on. Set the receiver's failsafe for the PTT channel to **"no pulses"** (preferred) or to the **released** position. Then test it: hold the button, switch the transmitter off, and check that it mutes.

**Hardware test checklist** (run `make test` first for the logic tests — no hardware needed):

1. PTT disabled: audio as before.
2. PTT enabled, transmitter off: muted, **SIGNAL LOST** (or READY if the receiver sends failsafe pulses at the released position).
3. Receiver on, button released: muted, **READY**, PWM near your released value.
4. Button held: **TALKING**, voice fades in.
5. Button released: fades out, **READY**.
6. Hold while speaking: voice sounds normal.
7. Rapid presses: no clicks or pops.
8. Transmitter off while held: mutes within the timeout.
9. Receiver unplugged: muted, **SIGNAL LOST**.
10. Change voice while released: switches as normal (silently).
11. Change voice while held: switches as normal (normal crossfade).
12. While released, the page's **periods** counter keeps rising: the DSP is still running; only the output is muted.

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
→ push-to-talk gate (both channels; only when PTT is enabled)
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
