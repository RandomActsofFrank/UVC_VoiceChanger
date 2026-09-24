# UVC VoiceChanger

Raspberry Pi port of the [s60sc ESP32 VoiceChanger](https://github.com/s60sc/ESP32_VoiceChanger). A Sound Blaster Play 3 is the microphone and the headphone output.

`ESP/` is the original ESP32 Arduino sketch, kept as a reference. The Pi app is at the repo root.

Same filters as the ESP32 build: low cut, high cut, band pass, shelves, peak, ring mod, clip, reverb, and pitch shift.

Audio runs at 48 kHz, which is the Play 3's usual USB rate. Filter frequencies are still in Hz, so the radio and dalek settings from the ESP README mean the same thing. Reverb is 100 ms, the same delay the ESP32 used (1600 samples at 16 kHz).

Record stores the dry microphone, after mic gain and before the filters. Play and Download run that recording through the filters that are on right now.

## Install on the Pi

```bash
sudo apt install python3-pip python3-venv portaudio19-dev
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python -m uvc
```

Plug the Play 3 in before starting. Open `http://<pi-address>:8080`.

Pass-through starts on its own. Mic gain 3 and amp volume 3 are unity, matching the original sliders. Radio and Dalek load the example settings from the ESP32 readme.

If the dongle is missing, the app uses the default sound device and the page says the Play 3 was not found.

## License

AGPL-3.0, same as the ESP32 original. The pitch shifter keeps Stephan Bernsee's Wide Open License notice. The biquad keeps Nigel Redmon's notice.
