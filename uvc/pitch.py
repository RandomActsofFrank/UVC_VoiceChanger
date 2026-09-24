"""
/****************************************************************************
*
* NAME: smbPitchShift.cpp
* VERSION: 1.2
* HOME URL: http://www.dspdimension.com
* KNOWN BUGS: none
*
* SYNOPSIS: Routine for doing pitch shifting while maintaining
* duration using the Short Time Fourier Transform.
*
* DESCRIPTION: The routine takes a pitchShift factor value which is between 0.5
* (one octave down) and 2. (one octave up). A value of exactly 1 does not change
* the pitch. numSampsToProcess tells the routine how many samples in indata[0...
* numSampsToProcess-1] should be pitch shifted and moved to outdata[0 ...
* numSampsToProcess-1]. The two buffers can be identical (ie. it can process the
* data in-place). fftFrameSize defines the FFT frame size used for the
* processing. Typical values are 1024, 2048 and 4096. It may be any value <=
* MAX_FRAME_LENGTH but it MUST be a power of 2. osamp is the STFT
* oversampling factor which also determines the overlap between adjacent STFT
* frames. It should at least be 4 for moderate scaling ratios. A value of 32 is
* recommended for best quality. sampleRate takes the sample rate for the signal
* in unit Hz, ie. 44100 for 44.1 kHz audio. The data passed to the routine in
* indata[] should be in the range [-1.0, 1.0), which is also the output range
* for the data, make sure you scale the data accordingly (for 16bit signed integers
* you would have to divide (and multiply) by 32768).
*
* COPYRIGHT 1999-2009 Stephan M. Bernsee
*
* The Wide Open License (WOL)
*
* Permission to use, copy, modify, distribute and sell this software and its
* documentation for any purpose is hereby granted without fee, provided that
* the above copyright notice and this license appear in all source copies.
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF
* ANY KIND. See http://www.dspguru.com/wol.htm for more information.
*
*****************************************************************************/
"""

# s60sc 2023 split the routine into init + chunk processing and used float.
# This Python port keeps that behavior. The FFT itself uses numpy, which
# matches the unnormalized transform in the original smbFft.

import math

import numpy as np

INT_FLT = 32768.0


class PitchShifter:
    def __init__(self, pitch_shift, fft_frame_size, osamp, sample_rate):
        self.pitch = float(pitch_shift)
        self.fft_frame_size = int(fft_frame_size)
        self.osamp = int(osamp)
        n = self.fft_frame_size
        self.step_size = n // self.osamp
        self.freq_per_bin = float(sample_rate) / float(n)
        self.expct = 2.0 * math.pi * float(self.step_size) / float(n)
        self.in_fifo_latency = n - self.step_size
        self.g_rover = self.in_fifo_latency
        self.g_in_fifo = np.zeros(n, dtype=np.float64)
        self.g_out_fifo = np.zeros(n, dtype=np.float64)
        self.g_last_phase = np.zeros(n // 2 + 1, dtype=np.float64)
        self.g_sum_phase = np.zeros(n // 2 + 1, dtype=np.float64)
        self.g_output_accum = np.zeros(2 * n, dtype=np.float64)
        k = np.arange(n, dtype=np.float64)
        self.window = -0.5 * np.cos(2.0 * math.pi * k / float(n)) + 0.5

    def process(self, samples):
        """Pitch-shift an int16 block. Returns a new int16 array."""
        incoming = np.asarray(samples, dtype=np.int16)
        out = np.empty(incoming.shape[0], dtype=np.int16)
        n = self.fft_frame_size
        latency = self.in_fifo_latency
        for i in range(incoming.shape[0]):
            self.g_in_fifo[self.g_rover] = float(incoming[i]) / INT_FLT
            value = self.g_out_fifo[self.g_rover - latency] * INT_FLT
            out[i] = np.int16(np.clip(value, -32768, 32767))
            self.g_rover += 1
            if self.g_rover >= n:
                self._process_frame()
                self.g_rover = latency
        return out

    def _process_frame(self):
        n = self.fft_frame_size
        n2 = n // 2
        windowed = self.g_in_fifo * self.window
        spec = np.fft.fft(windowed)
        bins = np.arange(n2 + 1, dtype=np.float64)
        real = spec.real[: n2 + 1]
        imag = spec.imag[: n2 + 1]
        magn = 2.0 * np.sqrt(real * real + imag * imag)
        phase = np.arctan2(imag, real)
        tmp = phase - self.g_last_phase
        self.g_last_phase = phase.copy()
        tmp = tmp - bins * self.expct
        qpd = np.trunc(tmp / math.pi).astype(np.int64)
        qpd = np.where(qpd >= 0, qpd + (qpd & 1), qpd - (qpd & 1))
        tmp = tmp - math.pi * qpd
        tmp = self.osamp * tmp / (2.0 * math.pi)
        ana_freq = bins * self.freq_per_bin + tmp * self.freq_per_bin

        syn_magn = np.zeros(n, dtype=np.float64)
        syn_freq = np.zeros(n, dtype=np.float64)
        for k in range(n2 + 1):
            index = int(k * self.pitch)
            if index <= n2:
                syn_magn[index] += magn[k]
                syn_freq[index] = ana_freq[k] * self.pitch

        tmp = syn_freq[: n2 + 1] - bins * self.freq_per_bin
        tmp = tmp / self.freq_per_bin
        tmp = 2.0 * math.pi * tmp / self.osamp
        tmp = tmp + bins * self.expct
        self.g_sum_phase += tmp
        rebuilt = np.zeros(n, dtype=np.complex128)
        rebuilt[: n2 + 1] = syn_magn[: n2 + 1] * np.exp(1j * self.g_sum_phase)
        # Original zeros every bin above Nyquist and leaves the forward FFT unnormalized.
        time = np.fft.ifft(rebuilt).real * n
        self.g_output_accum[:n] += 2.0 * self.window * time / (n2 * self.osamp)
        self.g_out_fifo[: self.step_size] = self.g_output_accum[: self.step_size]
        self.g_output_accum[:n] = self.g_output_accum[self.step_size : self.step_size + n]
        self.g_in_fifo[: self.in_fifo_latency] = self.g_in_fifo[
            self.step_size : self.step_size + self.in_fifo_latency
        ]
