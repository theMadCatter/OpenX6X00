/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Pure transverter math. No LVGL, no SQLite: tools/xvtr_test.c links this
 *  file alone. Persistence and the radio live in src/cfg/xvtr_cfg.cpp and
 *  src/radio.c.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "xvtr.h"

#include <string.h>

/* A fixed LO only covers XVTR_IF_MAX_HZ - XVTR_IF_MIN_HZ of RF (about 55 MHz)
 * on this radio. The preset LO is the weak-signal calling frequency minus a
 * 28 MHz IF, low-side, so the calling frequency lands inside the HF window.
 * The band row in the band list is wider; band_use uses the slice, the part
 * that actually maps into the IF. */
typedef struct {
    const char *name;
    int64_t     rf_from;
    int64_t     rf_to;
    int64_t     lo_hz;
} xvtr_preset_t;

static const xvtr_preset_t presets[] = {
    {"2m",    144000000LL,    150000000LL,    116000000LL},  /* 144-150, IF 28-34. 145.500 -> 29.500 */
    {"70cm",  432000000LL,    438000000LL,    404000000LL},  /* 432-438, IF 28-34 */
    {"1.25m", 222000000LL,    225000000LL,    194000000LL},  /* 222.1 inside */
    {"33cm",  902000000LL,    928000000LL,    874000000LL},  /* IF 28-54 */
    {"23cm",  1240000000LL,   1300000000LL,   1212000000LL}, /* slice ~1240-1267 */
    {"13cm",  2300000000LL,   2450000000LL,   2272000000LL}, /* 2304.1 inside */
    {"9cm",   3300000000LL,   3500000000LL,   3372000000LL}, /* slice ~3400-3427; RF below the LO is a negative IF */
    {"6cm",   5650000000LL,   5925000000LL,   5732000000LL}, /* 5760.1 inside */
    {"3cm",   10000000000LL,  10500000000LL,  10340000000LL}, /* 10368.1 inside */
    {"1.25cm", 24000000000LL, 24250000000LL,  24020000000LL}, /* 24048.1 inside */
};

void xvtr_preset(int index, int vhf_rx, xvtr_slot_t *out) {
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= (int)(sizeof(presets) / sizeof(presets[0]))) {
        return;
    }
    /* Name is a short literal; the buffer is larger than every preset name. */
    {
        const xvtr_preset_t *p = &presets[index];
        size_t n = 0;
        while (p->name[n] && n + 1 < XVTR_NAME_LEN) {
            out->name[n] = p->name[n];
            n++;
        }
        out->name[n] = '\0';
        out->rf_from = p->rf_from;
        out->rf_to   = p->rf_to;
        out->lo_hz   = p->lo_hz;
    }
    /* 2 m and 70 cm on by default only on a radio that does not already
     * receive them (the X6100). Everything else starts off, so a new radio
     * does not transmit on 10 m when the operator dials 145.500. */
    out->enabled = (index <= 1 && !vhf_rx) ? 1 : 0;
}

int64_t xvtr_lo(const xvtr_slot_t *s) {
    return s->lo_hz + s->cal_hz;
}

int xvtr_slice(const xvtr_slot_t *s, int64_t *rf_lo, int64_t *rf_hi) {
    int64_t lo = xvtr_lo(s);
    int64_t a, b;

    if (!s->enabled) {
        return 0;
    }
    if (s->high_side) {
        /* RF = LO - IF, so the low RF is the high IF. */
        a = lo - XVTR_IF_MAX_HZ;
        b = lo - XVTR_IF_MIN_HZ;
    } else {
        a = lo + XVTR_IF_MIN_HZ;
        b = lo + XVTR_IF_MAX_HZ;
    }
    if (a < s->rf_from) {
        a = s->rf_from;
    }
    if (b > s->rf_to) {
        b = s->rf_to;
    }
    if (a > b) {
        return 0;
    }
    if (rf_lo) {
        *rf_lo = a;
    }
    if (rf_hi) {
        *rf_hi = b;
    }
    return 1;
}

int64_t xvtr_if_hz(const xvtr_slot_t *s, int64_t rf) {
    int64_t lo = xvtr_lo(s);
    return s->high_side ? lo - rf : rf - lo;
}

static int overlaps(int64_t lo, int64_t hi, int64_t a, int64_t b) {
    return lo <= b && hi >= a;
}

int xvtr_span_use(const xvtr_slot_t *slots, int n, int64_t lo, int64_t hi) {
    int i;

    for (i = 0; i < n; i++) {
        int64_t a, b;
        if (!xvtr_slice(&slots[i], &a, &b)) {
            continue;
        }
        if (overlaps(lo, hi, a, b)) {
            return slots[i].rx_only ? XVTR_USE_RX : XVTR_USE_TX;
        }
    }
    return XVTR_USE_NONE;
}

const xvtr_slot_t *xvtr_for_rf(const xvtr_slot_t *slots, int n, int64_t rf) {
    int i;

    for (i = 0; i < n; i++) {
        int64_t a, b;
        if (xvtr_slice(&slots[i], &a, &b) && rf >= a && rf <= b) {
            return &slots[i];
        }
    }
    return NULL;
}

/* Edges in Hz, inclusive. HF edges are the ones the log has always used
 * (qso_log.c, kHz times 1000). Microwave edges are the ADIF band, which is
 * wider than the slice one LO can tune. No range overlaps another, so the
 * first match is the only match: "6cm" cannot fall through to "6m". */
typedef struct {
    int64_t     lo;
    int64_t     hi;
    const char *name;
} xvtr_adif_range_t;

static const xvtr_adif_range_t adif_ranges[] = {
    {1800000LL,      2000000LL,      "160m"},
    {3500000LL,      4000000LL,      "80m"},
    {5351000LL,      5367000LL,      "60m"},
    {7000000LL,      7300000LL,      "40m"},
    {10100000LL,     10150000LL,     "30m"},
    {14000000LL,     14350000LL,     "20m"},
    {18068000LL,     18168000LL,     "17m"},
    {21000000LL,     21450000LL,     "15m"},
    {24890000LL,     24990000LL,     "12m"},
    {28000000LL,     29700000LL,     "10m"},
    {50000000LL,     54000000LL,     "6m"},
    {70000000LL,     72000000LL,     "4m"},
    {144000000LL,    148000000LL,    "2m"},
    {222000000LL,    225000000LL,    "1.25m"},
    {420000000LL,    450000000LL,    "70cm"},
    {902000000LL,    928000000LL,    "33cm"},
    {1240000000LL,   1300000000LL,   "23cm"},
    {2300000000LL,   2450000000LL,   "13cm"},
    {3300000000LL,   3500000000LL,   "9cm"},
    {5650000000LL,   5925000000LL,   "6cm"},
    {10000000000LL,  10500000000LL,  "3cm"},
    {24000000000LL,  24250000000LL,  "1.25cm"},
};

const char *xvtr_adif_band(int64_t hz) {
    size_t i;

    for (i = 0; i < sizeof(adif_ranges) / sizeof(adif_ranges[0]); i++) {
        if (hz >= adif_ranges[i].lo && hz <= adif_ranges[i].hi) {
            return adif_ranges[i].name;
        }
    }
    return "";
}
