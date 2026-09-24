# Biquad
#
# Created by Nigel Redmon on 11/24/12
# EarLevel Engineering: earlevel.com
# Copyright 2012 Nigel Redmon
#
# For a complete explanation of the Biquad code:
# http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
#
# License:
#
# This source code is provided as is, without warranty.
# You may copy and distribute verbatim copies of this document.
# You may modify and use this source code to create binary code
# for your own purposes, free or commercial.
#
# s60sc 2021 converted the original from double to float.
# Ported to Python for the Raspberry Pi voice changer.

import math

import numpy as np

LOWPASS = 0
HIGHPASS = 1
BANDPASS = 2
NOTCH = 3
PEAK = 4
LOWSHELF = 5
HIGHSHELF = 6


class Biquad:
    def __init__(self, ftype=LOWPASS, fc=0.5, q=0.707, peak_gain_db=0.0):
        self.z1 = 0.0
        self.z2 = 0.0
        self.set_biquad(ftype, fc, q, peak_gain_db)

    def set_biquad(self, ftype, fc, q, peak_gain_db):
        self.type = ftype
        self.q = float(q)
        self.fc = float(fc)
        self.peak_gain = float(peak_gain_db)
        self._calc()

    def process(self, block):
        """Direct form transposed, one block of float samples."""
        x = np.asarray(block, dtype=np.float64)
        y = np.empty(x.shape[0], dtype=np.float64)
        z1 = self.z1
        z2 = self.z2
        a0 = self.a0
        a1 = self.a1
        a2 = self.a2
        b1 = self.b1
        b2 = self.b2
        for i in range(x.shape[0]):
            inp = x[i]
            out = inp * a0 + z1
            z1 = inp * a1 + z2 - b1 * out
            z2 = inp * a2 - b2 * out
            y[i] = out
        self.z1 = z1
        self.z2 = z2
        return y

    def _calc(self):
        v = 10.0 ** (abs(self.peak_gain) / 20.0)
        k = math.tan(math.pi * self.fc)
        q = self.q
        if self.type == LOWPASS:
            norm = 1.0 / (1.0 + k / q + k * k)
            self.a0 = k * k * norm
            self.a1 = 2.0 * self.a0
            self.a2 = self.a0
            self.b1 = 2.0 * (k * k - 1.0) * norm
            self.b2 = (1.0 - k / q + k * k) * norm
        elif self.type == HIGHPASS:
            norm = 1.0 / (1.0 + k / q + k * k)
            self.a0 = norm
            self.a1 = -2.0 * self.a0
            self.a2 = self.a0
            self.b1 = 2.0 * (k * k - 1.0) * norm
            self.b2 = (1.0 - k / q + k * k) * norm
        elif self.type == BANDPASS:
            norm = 1.0 / (1.0 + k / q + k * k)
            self.a0 = k / q * norm
            self.a1 = 0.0
            self.a2 = -self.a0
            self.b1 = 2.0 * (k * k - 1.0) * norm
            self.b2 = (1.0 - k / q + k * k) * norm
        elif self.type == NOTCH:
            norm = 1.0 / (1.0 + k / q + k * k)
            self.a0 = (1.0 + k * k) * norm
            self.a1 = 2.0 * (k * k - 1.0) * norm
            self.a2 = self.a0
            self.b1 = self.a1
            self.b2 = (1.0 - k / q + k * k) * norm
        elif self.type == PEAK:
            if self.peak_gain >= 0:
                norm = 1.0 / (1.0 + (1.0 / q) * k + k * k)
                self.a0 = (1.0 + (v / q) * k + k * k) * norm
                self.a1 = 2.0 * (k * k - 1.0) * norm
                self.a2 = (1.0 - (v / q) * k + k * k) * norm
                self.b1 = self.a1
                self.b2 = (1.0 - (1.0 / q) * k + k * k) * norm
            else:
                norm = 1.0 / (1.0 + (v / q) * k + k * k)
                self.a0 = (1.0 + (1.0 / q) * k + k * k) * norm
                self.a1 = 2.0 * (k * k - 1.0) * norm
                self.a2 = (1.0 - (1.0 / q) * k + k * k) * norm
                self.b1 = self.a1
                self.b2 = (1.0 - (v / q) * k + k * k) * norm
        elif self.type == LOWSHELF:
            if self.peak_gain >= 0:
                norm = 1.0 / (1.0 + math.sqrt(2.0) * k + k * k)
                self.a0 = (1.0 + math.sqrt(2.0 * v) * k + v * k * k) * norm
                self.a1 = 2.0 * (v * k * k - 1.0) * norm
                self.a2 = (1.0 - math.sqrt(2.0 * v) * k + v * k * k) * norm
                self.b1 = 2.0 * (k * k - 1.0) * norm
                self.b2 = (1.0 - math.sqrt(2.0) * k + k * k) * norm
            else:
                norm = 1.0 / (1.0 + math.sqrt(2.0 * v) * k + v * k * k)
                self.a0 = (1.0 + math.sqrt(2.0) * k + k * k) * norm
                self.a1 = 2.0 * (k * k - 1.0) * norm
                self.a2 = (1.0 - math.sqrt(2.0) * k + k * k) * norm
                self.b1 = 2.0 * (v * k * k - 1.0) * norm
                self.b2 = (1.0 - math.sqrt(2.0 * v) * k + v * k * k) * norm
        elif self.type == HIGHSHELF:
            if self.peak_gain >= 0:
                norm = 1.0 / (1.0 + math.sqrt(2.0) * k + k * k)
                self.a0 = (v + math.sqrt(2.0 * v) * k + k * k) * norm
                self.a1 = 2.0 * (k * k - v) * norm
                self.a2 = (v - math.sqrt(2.0 * v) * k + k * k) * norm
                self.b1 = 2.0 * (k * k - 1.0) * norm
                self.b2 = (1.0 - math.sqrt(2.0) * k + k * k) * norm
            else:
                norm = 1.0 / (v + math.sqrt(2.0 * v) * k + k * k)
                self.a0 = (1.0 + math.sqrt(2.0) * k + k * k) * norm
                self.a1 = 2.0 * (k * k - 1.0) * norm
                self.a2 = (1.0 - math.sqrt(2.0) * k + k * k) * norm
                self.b1 = 2.0 * (k * k - v) * norm
                self.b2 = (v - math.sqrt(2.0 * v) * k + k * k) * norm
        else:
            self.a0 = 1.0
            self.a1 = self.a2 = self.b1 = self.b2 = 0.0
