# Raspberry Pi voice changer

Same filters as the ESP32 VoiceChanger: low cut, high cut, band pass, shelves, peak, ring mod, clip, reverb, and pitch shift. The microphone and headphones are a Sound Blaster Play 3.

Audio runs at 48 kHz, which is the Play 3's usual USB rate. Filter frequencies are still in Hz, so the radio and dalek settings from the original README mean the same thing. Reverb is 100 ms, the same delay the ESP32 used (1600 samples at 16 kHz).

Record stores the dry microphone, after mic gain and before the filters. Play and Download run that recording through the filters that are on right now.

## Install on the Pi

```bash
sudo apt install python3-pip python3-venv portaudio19-dev
cd pi
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python -m uvc
```

Plug the Play 3 in before starting. Open `http://<pi-address>:8080`.

Pass-through starts on its own. Mic gain 3 and amp volume 3 are unity, matching the original sliders. Radio and Dalek load the example settings from the ESP32 readme.

If the dongle is missing, the app uses the default sound device and the page says the Play 3 was not found.
