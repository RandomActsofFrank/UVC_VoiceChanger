"""Live pass-through, dry recording, filtered playback, and WAV download."""

import io
import threading
import wave

import numpy as np

from uvc.device import BLOCK, open_stream
from uvc.effects import SAMPLE_RATE, EffectChain, Settings, apply_mic_gain

MAX_SECONDS = 180
MAX_SAMPLES = SAMPLE_RATE * MAX_SECONDS


class Engine:
    def __init__(self):
        self.settings = Settings()
        self.chain = EffectChain(self.settings)
        self.lock = threading.Lock()
        self.mode = "pass"
        self.dry_chunks = []
        self.dry = np.zeros(0, dtype=np.int16)
        self.recorded = 0
        self.play_pos = 0
        self.overflows = 0
        self.stream = None
        self.device = {"name": "", "play3": False, "channels": 0, "sample_rate": SAMPLE_RATE, "error": None}

    def start(self):
        self.stream, self.device = open_stream(self._callback)
        return self.device["error"] is None

    def stop(self):
        if self.stream is not None:
            self.stream.stop()
            self.stream.close()
            self.stream = None

    def update_settings(self, data):
        with self.lock:
            self.settings = Settings.from_dict(data)
            self.chain = EffectChain(self.settings)

    def apply_preset(self, name):
        with self.lock:
            if name == "radio":
                self.settings.radio()
            elif name == "dalek":
                self.settings.dalek()
            else:
                self.settings.flat()
            self.chain = EffectChain(self.settings)

    def set_mode(self, mode):
        with self.lock:
            if mode == "record":
                self.dry_chunks = []
                self.dry = np.zeros(0, dtype=np.int16)
                self.recorded = 0
                self.play_pos = 0
            elif mode == "play":
                self._finalize_recording()
                self.play_pos = 0
                if self.dry.size == 0:
                    return False
            elif mode == "idle":
                if self.mode == "record":
                    self._finalize_recording()
            self.mode = mode
            return True

    def status(self):
        with self.lock:
            recorded = self.recorded if self.mode == "record" else self.dry.size
            return {
                "mode": self.mode,
                "device": self.device,
                "overflows": self.overflows,
                "recorded_samples": int(recorded),
                "recorded_seconds": recorded / float(SAMPLE_RATE),
                "settings": self.settings.to_dict(),
            }

    def render_wav(self):
        """Filtered copy of the dry recording, matching the original download button."""
        with self.lock:
            self._finalize_recording()
            dry = self.dry.copy()
            settings = Settings.from_dict(self.settings.to_dict())
        if dry.size == 0:
            return None
        chain = EffectChain(settings)
        pieces = []
        for start in range(0, dry.size, BLOCK):
            pieces.append(chain.process(dry[start : start + BLOCK]))
        audio = np.concatenate(pieces) if pieces else dry
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(2)
            handle.setframerate(SAMPLE_RATE)
            handle.writeframes(audio.tobytes())
        return buffer.getvalue()

    def _finalize_recording(self):
        if not self.dry_chunks:
            return
        self.dry = np.concatenate(self.dry_chunks)
        self.dry_chunks = []
        self.recorded = int(self.dry.size)

    def _callback(self, indata, outdata, frames, _time, status):
        if status:
            self.overflows += 1
        if indata.shape[1] == 1:
            mono = indata[:, 0].copy()
        else:
            mono = ((indata[:, 0].astype(np.int32) + indata[:, 1].astype(np.int32)) // 2).astype(np.int16)

        with self.lock:
            mode = self.mode
            chain = self.chain
            mic_gain = self.settings.mic_gain
            gained = apply_mic_gain(mono, mic_gain)
            if mode == "record":
                room = MAX_SAMPLES - self.recorded
                if room <= 0:
                    self._finalize_recording()
                    self.mode = "pass"
                    mode = "pass"
                else:
                    piece = gained[:room].copy()
                    self.dry_chunks.append(piece)
                    self.recorded += int(piece.size)
            if mode == "play":
                if self.dry.size == 0:
                    source = np.zeros(frames, dtype=np.int16)
                    self.mode = "pass"
                else:
                    stop = min(self.play_pos + frames, self.dry.size)
                    source = np.zeros(frames, dtype=np.int16)
                    source[: stop - self.play_pos] = self.dry[self.play_pos : stop]
                    self.play_pos = stop
                    if self.play_pos >= self.dry.size:
                        self.mode = "pass"
            elif mode in ("pass", "record"):
                source = gained
            else:
                source = None

        if source is None:
            processed = np.zeros(frames, dtype=np.int16)
        else:
            processed = chain.process(source)

        if outdata.shape[1] == 1:
            outdata[:, 0] = processed
        else:
            outdata[:, 0] = processed
            outdata[:, 1] = processed
