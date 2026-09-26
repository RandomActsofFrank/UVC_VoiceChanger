/*
 * Push-to-talk logic tests (no GPIO or audio hardware needed): make test
 */

#include "ptt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        g_failed++;
    }
}

const int64_t kMs = 1000000;

/* Feeds 50 Hz servo pulses of the given width; returns the time after them. */
int64_t pulses(PttDecoder* d, int64_t t, int width_us, int count) {
    for (int i = 0; i < count; i++) {
        d->edge(true, t);
        d->edge(false, t + (int64_t)width_us * 1000);
        t += 20 * kMs;
    }
    return t;
}

void test_decoder() {
    PttConfig cfg;
    PttDecoder d;
    d.configure(cfg);

    int64_t t = 1000 * kMs;
    t = pulses(&d, t, 1000, 2);
    check(!d.locked() && !d.active(), "two pulses: not trusted yet");
    t = pulses(&d, t, 1000, 1);
    check(d.locked() && !d.active(), "released (1000 us): locked, PTT OFF");
    check(d.last_us() == 1000, "pulse width measured");

    t = pulses(&d, t, 2000, 1);
    check(d.active(), "held (2000 us): PTT ON");
    t = pulses(&d, t, 1500, 5);
    check(d.active(), "1500 us inside hysteresis band: stays ON");
    t = pulses(&d, t, 1300, 1);
    check(!d.active(), "at OFF threshold: PTT OFF");
    t = pulses(&d, t, 1600, 5);
    check(!d.active(), "1600 us inside band: stays OFF");
    t = pulses(&d, t, 1700, 1);
    check(d.active(), "at ON threshold: PTT ON");

    t = pulses(&d, t, 3000, 1);
    check(!d.locked() && !d.active(), "out-of-range pulse while ON: untrusted + OFF at once");
    t = pulses(&d, t, 2000, 2);
    check(!d.active(), "back in range: still OFF until the signal settles");
    t = pulses(&d, t, 2000, 1);
    check(d.active(), "settled and held: ON again");

    d.reset();
    check(!d.locked() && !d.active(), "reset (signal lost): OFF");
    t = pulses(&d, t, 1000, 3);
    check(d.locked() && !d.active(), "signal returns with button released: OFF, not the old ON");

    d.edge(false, t);
    check(!d.active(), "falling edge without a rising edge ignored");
    d.edge(true, t);
    d.drop_edge();
    check(!d.edge(false, t + 2 * kMs), "edge gap: no pulse paired across it");

    PttConfig inv;
    inv.on_us = 1200;
    inv.off_us = 1600;
    PttDecoder di;
    di.configure(inv);
    int64_t ti = pulses(&di, 0, 1900, 3);
    check(di.locked() && !di.active(), "inverted: long pulse = OFF");
    ti = pulses(&di, ti, 1100, 1);
    check(di.active(), "inverted: short pulse = ON");
}

void test_state() {
    const int64_t to = 100 * kMs;
    const int64_t now = 5000 * kMs;
    check(ptt_state_from(false, 0, now, to, false) == kPttDisabled, "disabled: output always allowed");
    check(ptt_state_from(true, 0, now, to, true) == kPttSignalLost, "enabled, never a pulse: lost");
    check(ptt_state_from(true, now - 50 * kMs, now, to, false) == kPttInactive, "fresh pulses, released: ready");
    check(ptt_state_from(true, now - 50 * kMs, now, to, true) == kPttActive, "fresh pulses, held: talking");
    check(ptt_state_from(true, now - 150 * kMs, now, to, true) == kPttSignalLost,
          "held but no pulse for > timeout (monitor stalled / TX off): lost");
    check(ptt_state_from(true, now + 10 * kMs, now, to, true) == kPttSignalLost, "timestamp from the future: lost");
}

void test_gate() {
    const unsigned int rate = 48000;
    const unsigned int period = 256;
    OutputGate g;
    g.init(rate, 10.0f);

    int16_t buf[period * 2];
    auto fill = [&]() {
        for (unsigned int i = 0; i < period * 2; i++) {
            buf[i] = 20000;
        }
    };

    fill();
    g.process(buf, period, 2, true);
    check(buf[0] == 20000 && buf[period * 2 - 1] == 20000 && g.gain() == 1.0f,
          "open from the start: samples untouched (PTT disabled = unchanged audio)");

    OutputGate m;
    m.init(rate, 10.0f);
    fill();
    m.process(buf, period, 2, false);
    check(buf[0] == 0 && buf[period * 2 - 1] == 0, "closed from the start: silent immediately");

    /* Unmute: ramp must be gradual (max step per sample) and reach 1 in ~10 ms. */
    int max_jump = 0;
    int16_t prev = 0;
    int blocks = 0;
    while (m.gain() < 1.0f && blocks < 100) {
        fill();
        m.process(buf, period, 2, true);
        for (unsigned int f = 0; f < period; f++) {
            const int jump = abs(buf[f * 2] - prev);
            max_jump = jump > max_jump ? jump : max_jump;
            prev = buf[f * 2];
            if (buf[f * 2] != buf[f * 2 + 1]) {
                max_jump = 99999;
            }
        }
        blocks++;
    }
    check(blocks >= 1 && blocks <= 2, "unmute completes in ~10 ms (<= 2 periods)");
    check(max_jump <= 50, "unmute ramp has no step bigger than ~0.2% of full scale (no click)");

    /* Rapid toggling every period: level never jumps. */
    max_jump = 0;
    for (int i = 0; i < 40; i++) {
        fill();
        m.process(buf, period, 2, (i & 1) != 0);
        for (unsigned int f = 0; f < period; f++) {
            const int jump = abs(buf[f * 2] - prev);
            max_jump = jump > max_jump ? jump : max_jump;
            prev = buf[f * 2];
        }
    }
    check(max_jump <= 50, "rapid presses: no discontinuities from the gate");

    fill();
    m.process(buf, period, 2, false);
    fill();
    m.process(buf, period, 2, false);
    check(m.gain() == 0.0f && buf[period * 2 - 1] == 0, "release: fully muted after the ramp");
}

}  // namespace

int main() {
    test_decoder();
    test_state();
    test_gate();
    printf(g_failed ? "\n%d test(s) FAILED\n" : "\nall PTT tests passed\n", g_failed);
    return g_failed ? 1 : 0;
}
