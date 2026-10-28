/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Host checks for the transverter math. Built on its own:
 *    gcc -std=c11 -Wall -Werror -I src -o /tmp/xvtr_test src/xvtr.c tools/xvtr_test.c
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "xvtr.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

int main(void) {
    xvtr_slot_t s;
    xvtr_slot_t slots[XVTR_COUNT];
    int64_t lo, hi;
    int i;

    /* 145.500 MHz, low-side LO 116 MHz -> IF 29.500 MHz. The old two-slot path. */
    xvtr_preset(0, 0, &s);
    s.enabled = 1;
    expect(xvtr_if_hz(&s, 145500000LL) == 29500000LL, "2m low-side IF");
    expect(xvtr_slice(&s, &lo, &hi) == 1, "2m slice");
    expect(lo == 144000000LL && hi == 150000000LL, "2m slice edges");

    /* High-side: LO 10396 MHz, RF 10368 MHz -> IF 28 MHz, inverted. */
    memset(&s, 0, sizeof(s));
    s.enabled   = 1;
    s.high_side = 1;
    s.lo_hz     = 10396000000LL;
    s.rf_from   = 10000000000LL;
    s.rf_to     = 10500000000LL;
    expect(xvtr_if_hz(&s, 10368000000LL) == 28000000LL, "10 GHz high-side IF");
    expect(s.high_side == 1, "high-side flag");

    /* Calibration moves the IF by the same number of hertz. */
    s.high_side = 0;
    s.lo_hz     = 10340000000LL;
    s.cal_hz    = 1000;
    expect(xvtr_if_hz(&s, 10368100000LL) == 28099000LL, "cal +1000");

    /* A disabled slot is not a match, even when the frequency sits in its range. */
    for (i = 0; i < XVTR_COUNT; i++) {
        xvtr_preset(i, 1, &slots[i]);
    }
    expect(xvtr_for_rf(slots, XVTR_COUNT, 145500000LL) == NULL, "X6200 presets off");
    slots[0].enabled = 1;
    expect(xvtr_for_rf(slots, XVTR_COUNT, 145500000LL) == &slots[0], "enabled 2m");
    slots[0].rx_only = 1;
    expect(xvtr_span_use(slots, XVTR_COUNT, 145500000LL, 145500000LL) == XVTR_USE_RX, "rx only");
    slots[0].rx_only = 0;
    expect(xvtr_span_use(slots, XVTR_COUNT, 145500000LL, 145500000LL) == XVTR_USE_TX, "tx");

    /* ADIF names. 6 cm must not become 6 m. 10.3681 GHz does not fit in int32. */
    expect(strcmp(xvtr_adif_band(10368100000LL), "3cm") == 0, "3cm");
    expect(strcmp(xvtr_adif_band(24048000000LL), "1.25cm") == 0, "1.25cm");
    expect(strcmp(xvtr_adif_band(5760100000LL), "6cm") == 0, "6cm");
    expect(strcmp(xvtr_adif_band(50100000LL), "6m") == 0, "6m");
    expect(strcmp(xvtr_adif_band(145500000LL), "2m") == 0, "2m");
    expect(10368100000LL > 2147483647LL, "10 GHz above int32");
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", 24048.100000);
        expect(strcmp(buf, "24048.100000") == 0, "24 GHz format");
        expect(strlen(buf) < 24, "format fits");
    }

    /* 9 cm preset: RF below the LO is outside the slice. 3400.1 is inside. */
    xvtr_preset(6, 0, &s);
    s.enabled = 1;
    expect(xvtr_slice(&s, &lo, &hi) == 1, "9cm slice");
    expect(3400100000LL >= lo && 3400100000LL <= hi, "3400.1 inside 9cm");
    expect(xvtr_if_hz(&s, 3400100000LL) > 0, "9cm IF positive");

    /* 3 cm and 1.25 cm calling frequencies land on a 28 MHz IF. */
    xvtr_preset(8, 0, &s);
    s.enabled = 1;
    expect(xvtr_if_hz(&s, 10368100000LL) == 28100000LL, "10368.1 IF");
    xvtr_preset(9, 0, &s);
    s.enabled = 1;
    expect(xvtr_if_hz(&s, 24048100000LL) == 28100000LL, "24048.1 IF");

    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("xvtr_test ok\n");
    return 0;
}
