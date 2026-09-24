"""Voice effect chain ported from Filters.cpp and the gain path in audio.cpp.

Derived from s60sc ESP32_VoiceChanger (AGPL-3.0).
https://github.com/s60sc/ESP32_VoiceChanger
"""

import math
from dataclasses import dataclass, asdict

import numpy as np

from uvc.biquad import BANDPASS, HIGHPASS, HIGHSHELF, LOWPASS, LOWSHELF, PEAK, Biquad
from uvc.pitch import PitchShifter

SAMPLE_RATE = 48000
# Original reverb is 1600 samples at 16 kHz, which is 100 ms.
REVERB_SAMPLES = 1600 * SAMPLE_RATE // 16000
MIC_GAIN_CENTER = 3
FFT_FRAME = 1024
OSAMP = 4
MAX_FILTERS = 10
INT16_MIN = -32768
INT16_MAX = 32767


def _cascade_offset(cascade):
    """Same offset the original `factorial()` computes: the sum 1..n, not n!."""
    top = cascade - 1
    total = 0
    for i in range(1, top + 1):
        total += i
    return total


def _butterworth_q():
    values = []
    for cascades in range(1, 9):
        for i in range(cascades):
            values.append(1.0 / (2.0 * math.cos((1 + i * 2) * math.pi / (cascades * 4))))
    return values


QVALS = _butterworth_q()


@dataclass
class Settings:
    mic_gain: int = 3
    amp_vol: int = 3
    disable: bool = False
    band_pass: bool = False
    bp_freq: float = 4000.0
    bp_q: float = 0.7
    bp_cas: int = 1
    high_pass: bool = False
    hp_freq: float = 4000.0
    hp_q: float = 0.7
    hp_cas: int = 1
    low_pass: bool = False
    lp_freq: float = 4000.0
    lp_q: float = 0.7
    lp_cas: int = 1
    high_shelf: bool = False
    hs_freq: float = 4000.0
    hs_gain: float = 3.0
    low_shelf: bool = False
    ls_freq: float = 4000.0
    ls_gain: float = 3.0
    peak: bool = False
    pk_freq: float = 4000.0
    pk_q: float = 0.7
    pk_gain: float = 3.0
    ring: bool = False
    sw_freq: int = 80
    sw_amp: int = 5
    clipping: bool = False
    clip_factor: int = 1
    reverb: bool = False
    decay_factor: int = 1
    pitch: float = 1.0

    def to_dict(self):
        return asdict(self)

    @classmethod
    def from_dict(cls, data):
        current = cls()
        for key, value in data.items():
            if not hasattr(current, key):
                continue
            current_value = getattr(current, key)
            if isinstance(current_value, bool):
                if isinstance(value, str):
                    value = value.lower() in ("1", "true", "yes", "on")
                else:
                    value = bool(value)
            else:
                value = type(current_value)(value)
            setattr(current, key, value)
        current._clamp()
        return current

    def _clamp(self):
        self.mic_gain = int(np.clip(self.mic_gain, 0, 11))
        self.amp_vol = int(np.clip(self.amp_vol, 0, 10))
        self.bp_cas = int(np.clip(self.bp_cas, 1, 8))
        self.hp_cas = int(np.clip(self.hp_cas, 1, 8))
        self.lp_cas = int(np.clip(self.lp_cas, 1, 8))
        self.sw_freq = max(1, int(self.sw_freq))
        self.sw_amp = int(np.clip(self.sw_amp, 1, 127))
        self.clip_factor = max(1, int(self.clip_factor))
        self.decay_factor = max(0, int(self.decay_factor))
        self.pitch = float(np.clip(self.pitch, 0.5, 2.0))

    def radio(self):
        """README example: low cut 1500 x2, high cut 2000, low shelf, peak."""
        self.disable = False
        self.band_pass = False
        self.high_pass = True
        self.hp_freq = 1500
        self.hp_q = 0.7
        self.hp_cas = 2
        self.low_pass = True
        self.lp_freq = 2000
        self.lp_q = 0.7
        self.lp_cas = 1
        self.low_shelf = True
        self.ls_freq = 2500
        self.ls_gain = 6
        self.high_shelf = False
        self.peak = True
        self.pk_freq = 400
        self.pk_q = 0.7
        self.pk_gain = 3
        self.ring = False
        self.clipping = False
        self.reverb = False
        self.pitch = 1.0

    def dalek(self):
        """README example: low cut 100, high cut 2000, ring mod at 50 Hz."""
        self.disable = False
        self.band_pass = False
        self.high_pass = True
        self.hp_freq = 100
        self.hp_q = 0.7
        self.hp_cas = 1
        self.low_pass = True
        self.lp_freq = 2000
        self.lp_q = 0.7
        self.lp_cas = 1
        self.low_shelf = False
        self.high_shelf = False
        self.peak = False
        self.ring = True
        self.sw_freq = 50
        self.sw_amp = 5
        self.clipping = False
        self.reverb = False
        self.pitch = 1.0

    def flat(self):
        self.disable = False
        self.band_pass = False
        self.high_pass = False
        self.low_pass = False
        self.high_shelf = False
        self.low_shelf = False
        self.peak = False
        self.ring = False
        self.clipping = False
        self.reverb = False
        self.pitch = 1.0


def apply_mic_gain(samples, mic_gain):
    steps = int(mic_gain) - MIC_GAIN_CENTER
    if steps == 0:
        return samples
    shift = min(8, abs(steps))
    values = samples.astype(np.int32)
    if steps > 0:
        values = values << shift
    else:
        values = values >> shift
    return np.clip(values, INT16_MIN, INT16_MAX).astype(np.int16)


def apply_volume(samples, amp_vol):
    adj = int(amp_vol) * 2
    if not adj:
        return samples
    adj = adj - 5 if adj > 5 else adj - 7
    if adj < 0:
        scaled = np.trunc(samples.astype(np.float64) / abs(adj))
    else:
        scaled = samples.astype(np.int32) * adj
    return np.clip(scaled, INT16_MIN, INT16_MAX).astype(np.int16)


class EffectChain:
    def __init__(self, settings):
        self.settings = settings
        self.filters = []
        self.sine = np.zeros(1, dtype=np.int16)
        self.sine_pos = 0
        self.reverb_buf = np.zeros(REVERB_SAMPLES, dtype=np.int16)
        self.reverb_ptr = 0
        self.pitch_shifter = None
        self.rebuild()

    def rebuild(self):
        self.settings._clamp()
        self.filters = []
        cfg = self.settings
        if cfg.band_pass:
            self._add(BANDPASS, cfg.bp_freq, cfg.bp_q, 0.0, cfg.bp_cas)
        if cfg.high_pass:
            self._add(HIGHPASS, cfg.hp_freq, cfg.hp_q, 0.0, cfg.hp_cas)
        if cfg.low_pass:
            self._add(LOWPASS, cfg.lp_freq, cfg.lp_q, 0.0, cfg.lp_cas)
        if cfg.high_shelf:
            self._add(HIGHSHELF, cfg.hs_freq, 1.0, cfg.hs_gain, 1)
        if cfg.low_shelf:
            self._add(LOWSHELF, cfg.ls_freq, 1.0, cfg.ls_gain, 1)
        if cfg.peak:
            self._add(PEAK, cfg.pk_freq, cfg.pk_q, cfg.pk_gain, 1)
        self._make_sine()
        self.reverb_buf[:] = 0
        self.reverb_ptr = 0
        if cfg.pitch != 1.0:
            self.pitch_shifter = PitchShifter(cfg.pitch, FFT_FRAME, OSAMP, SAMPLE_RATE)
        else:
            self.pitch_shifter = None

    def _add(self, ftype, freq, q_value, gain, cascade):
        cascade = max(1, int(cascade))
        cutoff = freq / float(SAMPLE_RATE)
        if cutoff > 0.5:
            cutoff = 0.5
        offset = _cascade_offset(cascade)
        for i in range(cascade):
            if len(self.filters) >= MAX_FILTERS:
                return
            q = q_value if cascade == 1 else QVALS[offset + i]
            self.filters.append(Biquad(ftype, cutoff, q, gain))

    def _make_sine(self):
        freq = max(1, int(self.settings.sw_freq))
        amp = max(1, int(self.settings.sw_amp))
        points = max(1, SAMPLE_RATE // freq)
        table = np.empty(points, dtype=np.int16)
        for i in range(points):
            table[i] = int(math.sin(math.pi * 2.0 * freq * i / SAMPLE_RATE) * amp)
        self.sine = table
        self.sine_pos = 0

    def process(self, samples):
        block = np.array(samples, dtype=np.int16, copy=True)
        cfg = self.settings
        if not cfg.disable:
            audio = block.astype(np.float64)
            for filt in self.filters:
                audio = filt.process(audio)
            block = np.clip(np.trunc(audio), INT16_MIN, INT16_MAX).astype(np.int16)
            if cfg.ring and self.sine.size:
                for i in range(block.shape[0]):
                    mixed = int(block[i]) * int(self.sine[self.sine_pos])
                    block[i] = np.int16(mixed // cfg.sw_amp)
                    self.sine_pos = (self.sine_pos + 1) % self.sine.size
            if cfg.reverb:
                decay = int(cfg.decay_factor) + 1
                for i in range(block.shape[0]):
                    delayed = int(self.reverb_buf[self.reverb_ptr]) // decay
                    mixed = np.int16(np.int32(block[i]) + np.int32(delayed))
                    block[i] = mixed
                    self.reverb_buf[self.reverb_ptr] = mixed
                    self.reverb_ptr = (self.reverb_ptr + 1) % REVERB_SAMPLES
        block = apply_volume(block, cfg.amp_vol)
        if self.pitch_shifter is not None:
            block = self.pitch_shifter.process(block)
        if not cfg.disable and cfg.clipping:
            clip = 1.0 + cfg.clip_factor / 6.0
            audio = block.astype(np.float64) / INT16_MAX
            shaped = audio * clip
            curved = (1.0 / clip) * (shaped / (1.0 + 0.28 * shaped * shaped))
            block = np.clip(np.trunc(INT16_MAX * curved), INT16_MIN, INT16_MAX).astype(np.int16)
        return block
