"""Sound Blaster Play 3 duplex capture and playback."""

import sounddevice as sd

SAMPLE_RATE = 48000
BLOCK = 512


def _is_play3(name):
    lowered = name.lower()
    return "sound blaster" in lowered or "play! 3" in lowered or "play 3" in lowered


def find_play3():
    """Return a sounddevice index, an (input, output) pair, or None."""
    inputs = []
    outputs = []
    for index, device in enumerate(sd.query_devices()):
        if not _is_play3(device["name"]):
            continue
        if device["max_input_channels"] > 0:
            inputs.append(index)
        if device["max_output_channels"] > 0:
            outputs.append(index)
    if not inputs or not outputs:
        return None
    if inputs[0] == outputs[0]:
        return inputs[0]
    return (inputs[0], outputs[0])


def device_label(device):
    if device is None:
        return "default audio device"
    if isinstance(device, tuple):
        names = [sd.query_devices(part)["name"] for part in device]
        return " -> ".join(names)
    return sd.query_devices(device)["name"]


def open_stream(callback):
    """Open a duplex stream on the Play 3. Falls back to the default device."""
    device = find_play3()
    label = device_label(device)
    using_play3 = device is not None
    last_error = None
    for channels in (2, 1):
        try:
            stream = sd.Stream(
                device=device,
                samplerate=SAMPLE_RATE,
                blocksize=BLOCK,
                dtype="int16",
                channels=channels,
                callback=callback,
            )
            stream.start()
            return stream, {
                "name": label,
                "play3": using_play3,
                "channels": channels,
                "sample_rate": SAMPLE_RATE,
                "error": None,
            }
        except Exception as exc:
            last_error = exc
    return None, {
        "name": label,
        "play3": using_play3,
        "channels": 0,
        "sample_rate": SAMPLE_RATE,
        "error": str(last_error),
    }
