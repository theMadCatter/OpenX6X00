/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Host test of the band plan tables and of the callsign that picks one. The strip, the settings
 *  and LVGL are not in this file: band_plan_data.c is the part that can be compiled on its own.
 *
 *    gcc -std=c11 -Wall -Wextra -Werror -I src -o /tmp/band_plan_test \
 *        src/band_plan_data.c tools/band_plan_test.c
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "band_plan_data.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_int(const char *what, int got, int want) {
    if (got == want) {
        return;
    }
    fprintf(stderr, "FAIL %s: got %d, want %d\n", what, got, want);
    fails++;
}

/* The segment type at hz, or -1. end is exclusive, as the strip treats it.
 * cls is ignored unless plan is the USA plan. */
static int type_at(int plan, int cls, int32_t hz) {
    const bp_seg_t *t = NULL;
    int             n = band_plan_table(plan, cls, &t);
    int             i;

    for (i = 0; i < n; i++) {
        if (hz >= t[i].start && hz < t[i].end) {
            return t[i].type;
        }
    }
    return -1;
}

/* Sorted, no overlap, a known type, and small enough for the strip's array. */
static void check_table(int plan, int cls, const char *name, int want_count) {
    const bp_seg_t *t = NULL;
    int             n = band_plan_table(plan, cls, &t);
    int             i;
    char            what[64];

    snprintf(what, sizeof(what), "%s count", name);
    if (want_count >= 0) {
        expect_int(what, n, want_count);
    } else if (n <= 0 || n > 160) {
        fprintf(stderr, "FAIL %s: count %d\n", name, n);
        fails++;
    }
    for (i = 0; i < n; i++) {
        if (t[i].start >= t[i].end || t[i].type < BP_CW || t[i].type > BP_BCN) {
            fprintf(stderr, "FAIL %s seg %d: %d..%d type %d\n", name, i, t[i].start, t[i].end, t[i].type);
            fails++;
        }
        if (i > 0 && t[i].start < t[i - 1].end) {
            fprintf(stderr, "FAIL %s overlap %d: %d < %d\n", name, i, t[i].start, t[i - 1].end);
            fails++;
        }
        if (i > 0 && t[i].start < t[i - 1].start) {
            fprintf(stderr, "FAIL %s unsorted %d\n", name, i);
            fails++;
        }
    }
}

static void check_call(const char *call, int want) {
    char what[64];

    snprintf(what, sizeof(what), "from_call(%s)", call ? call : "NULL");
    expect_int(what, band_plan_from_call(call), want);
    snprintf(what, sizeof(what), "resolve auto %s", call ? call : "NULL");
    expect_int(what, band_plan_resolve(BP_PLAN_AUTO, call), want);
}

int main(void) {
    const bp_seg_t *r1 = NULL;
    int             n;

    check_table(BP_PLAN_R1, BP_CLASS_EXTRA, "R1", 42);
    check_table(BP_PLAN_R2, BP_CLASS_EXTRA, "R2", -1);
    check_table(BP_PLAN_R3, BP_CLASS_EXTRA, "R3", -1);
    check_table(BP_PLAN_USA, BP_CLASS_EXTRA, "USA", -1);
    check_table(BP_PLAN_USA, BP_CLASS_ADVANCED, "USA Advanced", -1);
    check_table(BP_PLAN_USA, BP_CLASS_GENERAL, "USA General", -1);
    check_table(BP_PLAN_USA, BP_CLASS_TECHNICIAN, "USA Technician", -1);
    check_table(BP_PLAN_USA, BP_CLASS_NOVICE, "USA Novice", -1);
    check_table(BP_PLAN_USA, 99, "USA unknown class is Extra", -1);
    expect_int("auto has no table", band_plan_table(BP_PLAN_AUTO, BP_CLASS_EXTRA, &r1), 0);
    expect_int("unknown has no table", band_plan_table(99, BP_CLASS_EXTRA, &r1), 0);

    /* Region 1, kept as the radio shipped it. */
    n = band_plan_table(BP_PLAN_R1, BP_CLASS_TECHNICIAN, &r1);
    expect_int("R1 first", r1 && n > 0 ? r1[0].start : 0, 1810000);
    expect_int("R1 first type", r1 && n > 0 ? (int)r1[0].type : -1, BP_CW);
    expect_int("R1 15m narrow", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 21120000), BP_CW);
    expect_int("R1 15m still narrow", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 21148999), BP_CW);
    expect_int("R1 6m end inside", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 51999999), BP_ALL);
    expect_int("R1 6m stops at 52", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 52000000), -1);
    expect_int("R1 FT8 20m", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 14074000), BP_DIG);
    expect_int("R1 beacon 20m", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 14100000), BP_BCN);
    expect_int("R1 40m phone", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 7100000), BP_ALL);
    expect_int("R1 80m stops at 3.8", type_at(BP_PLAN_R1, BP_CLASS_EXTRA, 3900000), -1);

    /* Region 2 extends 80 m, 40 m and 6 m. */
    expect_int("R2 3.9 phone", type_at(BP_PLAN_R2, BP_CLASS_EXTRA, 3900000), BP_ALL);
    expect_int("R2 7.25 phone", type_at(BP_PLAN_R2, BP_CLASS_EXTRA, 7250000), BP_ALL);
    expect_int("R2 40m CW", type_at(BP_PLAN_R2, BP_CLASS_EXTRA, 7035000), BP_CW);
    expect_int("R2 6m beacon", type_at(BP_PLAN_R2, BP_CLASS_EXTRA, 50050000), BP_BCN);
    expect_int("R2 6m to 54", type_at(BP_PLAN_R2, BP_CLASS_EXTRA, 53000000), BP_ALL);

    /* Region 3: 80 m ends at 3.9 (exclusive), 40 m at 7.2, phone from 7030. */
    expect_int("R3 3.8 phone", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 3800000), BP_ALL);
    expect_int("R3 3.9 is the end", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 3900000), -1);
    expect_int("R3 7.25 not allocated", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 7250000), -1);
    expect_int("R3 7.035 phone", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 7035000), BP_ALL);
    expect_int("R3 30m gap left for the band list", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 10105000), -1);
    expect_int("R3 6m to 54", type_at(BP_PLAN_R3, BP_CLASS_EXTRA, 53000000), BP_ALL);

    /* USA: Extra phone edges, data windows, 60 m channels, 6 m to 54. */
    expect_int("USA 20m phone", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 14160000), BP_ALL);
    expect_int("USA 20m data", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 14074000), BP_DIG);
    expect_int("USA 20m still data at 14.149", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 14149000), BP_DIG);
    expect_int("USA FT8 40m", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 7074000), BP_DIG);
    expect_int("USA 40m phone at 7.125", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 7125000), BP_ALL);
    expect_int("USA 7.110 still CW", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 7110000), BP_CW);
    expect_int("USA 7.075 not phone", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 7075000), BP_DIG);
    expect_int("USA 40m CW", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 7035000), BP_CW);
    expect_int("USA 75m phone", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 3600000), BP_ALL);
    expect_int("USA FT8 80m", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 3573000), BP_DIG);
    expect_int("USA 80m CW", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 3500000), BP_CW);
    expect_int("USA 60m channel", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 5332000), BP_ALL);
    expect_int("USA 60m gap", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 5350000), -1);
    expect_int("USA 60m segment CW", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 5352000), BP_CW);
    expect_int("USA 6m to 54", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 53000000), BP_ALL);
    expect_int("USA 6m beacon", type_at(BP_PLAN_USA, BP_CLASS_EXTRA, 50070000), BP_BCN);

    check_call(NULL, BP_PLAN_R1);
    check_call("", BP_PLAN_R1);
    check_call("W1AW", BP_PLAN_USA);
    check_call("kc7bje", BP_PLAN_USA);
    check_call("AA1AA", BP_PLAN_USA);
    check_call("AL7", BP_PLAN_USA);
    check_call("KH2", BP_PLAN_USA);
    check_call("KH6/W1AW", BP_PLAN_USA);
    check_call("W1AW/KH6", BP_PLAN_USA);
    check_call("W1AW/P", BP_PLAN_USA);
    check_call("W1AW/MM", BP_PLAN_USA);
    check_call("JA1ABC", BP_PLAN_R3);
    check_call("on6ure", BP_PLAN_R1);
    check_call("  ve3abc ", BP_PLAN_R2);
    check_call("3D2AG", BP_PLAN_R3);
    check_call("3DA0XX", BP_PLAN_R1);
    check_call("VP6DX", BP_PLAN_R3);
    check_call("VP8XX", BP_PLAN_R2);
    check_call("AM1ABC", BP_PLAN_R1);
    check_call("A65XX", BP_PLAN_R1);
    check_call("BA4SI", BP_PLAN_R3);
    check_call("B1", BP_PLAN_R3);
    check_call("EP3", BP_PLAN_R3);
    check_call("JT1AA", BP_PLAN_R1);
    check_call("UA9", BP_PLAN_R1);
    check_call("OX3XX", BP_PLAN_R2);
    check_call("PJ2/W1AW", BP_PLAN_R2);
    check_call("W1AW/PJ2", BP_PLAN_R2);
    check_call("FP/W1AW", BP_PLAN_R2);
    check_call("DL/W1AW", BP_PLAN_R1);
    check_call("W1AW/DL", BP_PLAN_R1);
    check_call("DL/W1AW/P", BP_PLAN_R1);
    check_call("4U1UN", BP_PLAN_R1);

    /* An explicit plan ignores the callsign. A value this build does not know is Region 1. */
    expect_int("explicit R1 with a US call", band_plan_resolve(BP_PLAN_R1, "W1AW"), BP_PLAN_R1);
    expect_int("explicit USA with no call", band_plan_resolve(BP_PLAN_USA, ""), BP_PLAN_USA);
    expect_int("unknown setting", band_plan_resolve(99, "W1AW"), BP_PLAN_R1);

    /* A class the build does not know is Extra. Region 1 ignores the class. */
    expect_int("unknown class is Extra phone", type_at(BP_PLAN_USA, 99, 7125000), BP_ALL);
    expect_int("R1 ignores class", type_at(BP_PLAN_R1, BP_CLASS_TECHNICIAN, 7100000), BP_ALL);

    /* Advanced, 97.301(c). Phone on 40 m is 7.125. 14.150-14.175 and 21.200-21.225 are holes. */
    expect_int("Adv 40m phone", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 7125000), BP_ALL);
    expect_int("Adv below 7.025", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 7020000), -1);
    expect_int("Adv 40m CW", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 7025000), BP_CW);
    expect_int("Adv 20m phone", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 14175000), BP_ALL);
    expect_int("Adv 20m hole", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 14160000), -1);
    expect_int("Adv 20m data", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 14149000), BP_DIG);
    expect_int("Adv below 14.025", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 14000000), -1);
    expect_int("Adv 20m CW", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 14025000), BP_CW);
    expect_int("Adv 75m phone", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 3700000), BP_ALL);
    expect_int("Adv 80m hole", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 3650000), -1);
    expect_int("Adv 80m data ends", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 3600000), -1);
    expect_int("Adv FT8 80m", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 3573000), BP_DIG);
    expect_int("Adv 15m phone", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 21225000), BP_ALL);
    expect_int("Adv 15m hole", type_at(BP_PLAN_USA, BP_CLASS_ADVANCED, 21210000), -1);

    /* General, 97.301(d). Phone 7.175, 14.225, 21.275. 7.125-7.175 is a hole. */
    expect_int("Gen 40m phone", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 7175000), BP_ALL);
    expect_int("Gen 40m hole", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 7125000), -1);
    expect_int("Gen 40m CW", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 7110000), BP_CW);
    expect_int("Gen FT8 40m", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 7074000), BP_DIG);
    expect_int("Gen below 7.025", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 7000000), -1);
    expect_int("Gen 20m phone", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 14225000), BP_ALL);
    expect_int("Gen 20m hole", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 14200000), -1);
    expect_int("Gen 14.175 hole", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 14175000), -1);
    expect_int("Gen 75m phone", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 3800000), BP_ALL);
    expect_int("Gen 3.7 hole", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 3700000), -1);
    expect_int("Gen 15m phone", type_at(BP_PLAN_USA, BP_CLASS_GENERAL, 21275000), BP_ALL);

    /* Technician: HF CW subbands, 10 m phone 28.3-28.5, 6 m as Extra. */
    expect_int("Tech 80m CW", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 3525000), BP_CW);
    expect_int("Tech no 3.500", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 3500000), -1);
    expect_int("Tech 3.573 is CW", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 3573000), BP_CW);
    expect_int("Tech 10m phone", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 28300000), BP_ALL);
    expect_int("Tech 10m phone ends", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 28500000), -1);
    expect_int("Tech 10m data", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 28074000), BP_DIG);
    expect_int("Tech 6m", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 53000000), BP_ALL);
    expect_int("Tech 6m beacon", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 50070000), BP_BCN);
    expect_int("Tech 15m CW", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 21025000), BP_CW);
    expect_int("Tech below 21.025", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 21000000), -1);
    expect_int("Tech 15m ends", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 21200000), -1);
    expect_int("Tech no 20m", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 14000000), -1);
    expect_int("Tech 40m ends", type_at(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 7125000), -1);

    /* Novice HF matches Technician. No 6 m. */
    expect_int("Nov 10m phone", type_at(BP_PLAN_USA, BP_CLASS_NOVICE, 28300000), BP_ALL);
    expect_int("Nov 80m CW", type_at(BP_PLAN_USA, BP_CLASS_NOVICE, 3525000), BP_CW);
    expect_int("Nov no 6m", type_at(BP_PLAN_USA, BP_CLASS_NOVICE, 53000000), -1);

    /* The warning is a colour. These calls do not refuse a transmission. */
    expect_int("Gen warns in the 40m hole", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_GENERAL, 7150000), 1);
    expect_int("Gen phone is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_GENERAL, 7180000), 0);
    expect_int("Tech warns on 20m", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 14200000), 1);
    expect_int("Extra 20m is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_EXTRA, 14200000), 0);
    expect_int("Nov warns on 6m", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_NOVICE, 50100000), 1);
    expect_int("Tech 6m is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 50100000), 0);
    expect_int("WWV does not warn", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_EXTRA, 10000000), 0);
    expect_int("class ignored off the USA plan", band_plan_tx_warn(BP_PLAN_R1, BP_CLASS_TECHNICIAN, 14200000), 0);
    expect_int("Nov warns on 2m", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_NOVICE, 146000000), 1);
    expect_int("Tech 2m is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 146000000), 0);
    expect_int("Nov 1.25m is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_NOVICE, 222500000), 0);
    expect_int("Nov 23m subband is quiet", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_NOVICE, 1280000000), 0);
    expect_int("Nov warns on the rest of 23cm", band_plan_tx_warn(BP_PLAN_USA, BP_CLASS_NOVICE, 1250000000), 1);

    /* Gap fill keeps privileges and frequencies that are not a known amateur allocation. */
    {
        int32_t s[4], e[4];
        int     nspan;

        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 14000000, 14350000, s, e, 4);
        expect_int("Tech 20m gap dropped", nspan, 0);
        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_GENERAL, 7000000, 7300000, s, e, 4);
        expect_int("Gen 40m pieces", nspan, 2);
        expect_int("Gen 40m CW start", nspan >= 1 ? s[0] : 0, 7025000);
        expect_int("Gen 40m CW end", nspan >= 1 ? e[0] : 0, 7125000);
        expect_int("Gen 40m phone start", nspan >= 2 ? s[1] : 0, 7175000);
        expect_int("Gen 40m phone end", nspan >= 2 ? e[1] : 0, 7300000);
        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_EXTRA, 7000000, 7300000, s, e, 4);
        expect_int("Extra 40m one piece", nspan, 1);
        expect_int("Extra 40m start", nspan >= 1 ? s[0] : 0, 7000000);
        expect_int("Extra 40m end", nspan >= 1 ? e[0] : 0, 7300000);
        nspan = band_plan_keep_span(BP_PLAN_R1, BP_CLASS_TECHNICIAN, 14000000, 14350000, s, e, 4);
        expect_int("R1 span kept", nspan, 1);
        expect_int("R1 span start", nspan >= 1 ? s[0] : 0, 14000000);
        expect_int("R1 span end", nspan >= 1 ? e[0] : 0, 14350000);
        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_NOVICE, 144000000, 148000000, s, e, 4);
        expect_int("Nov 2m dropped", nspan, 0);
        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 144000000, 148000000, s, e, 4);
        expect_int("Tech 2m kept", nspan, 1);
        nspan = band_plan_keep_span(BP_PLAN_USA, BP_CLASS_TECHNICIAN, 10000000, 10100000, s, e, 4);
        expect_int("WWV kept", nspan, 1);
    }

    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("band plan data ok\n");
    return 0;
}
