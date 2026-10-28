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

/* The segment type at hz, or -1. end is exclusive, as the strip treats it. */
static int type_at(int plan, int32_t hz) {
    const bp_seg_t *t = NULL;
    int             n = band_plan_table(plan, &t);
    int             i;

    for (i = 0; i < n; i++) {
        if (hz >= t[i].start && hz < t[i].end) {
            return t[i].type;
        }
    }
    return -1;
}

/* Sorted, no overlap, a known type, and small enough for the strip's array. */
static void check_table(int plan, const char *name, int want_count) {
    const bp_seg_t *t = NULL;
    int             n = band_plan_table(plan, &t);
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

    check_table(BP_PLAN_R1, "R1", 42);
    check_table(BP_PLAN_R2, "R2", -1);
    check_table(BP_PLAN_R3, "R3", -1);
    check_table(BP_PLAN_USA, "USA", -1);
    expect_int("auto has no table", band_plan_table(BP_PLAN_AUTO, &r1), 0);
    expect_int("unknown has no table", band_plan_table(99, &r1), 0);

    /* Region 1, kept as the radio shipped it. */
    n = band_plan_table(BP_PLAN_R1, &r1);
    expect_int("R1 first", r1 && n > 0 ? r1[0].start : 0, 1810000);
    expect_int("R1 first type", r1 && n > 0 ? (int)r1[0].type : -1, BP_CW);
    expect_int("R1 15m narrow", type_at(BP_PLAN_R1, 21120000), BP_CW);
    expect_int("R1 15m still narrow", type_at(BP_PLAN_R1, 21148999), BP_CW);
    expect_int("R1 6m end inside", type_at(BP_PLAN_R1, 51999999), BP_ALL);
    expect_int("R1 6m stops at 52", type_at(BP_PLAN_R1, 52000000), -1);
    expect_int("R1 FT8 20m", type_at(BP_PLAN_R1, 14074000), BP_DIG);
    expect_int("R1 beacon 20m", type_at(BP_PLAN_R1, 14100000), BP_BCN);
    expect_int("R1 40m phone", type_at(BP_PLAN_R1, 7100000), BP_ALL);
    expect_int("R1 80m stops at 3.8", type_at(BP_PLAN_R1, 3900000), -1);

    /* Region 2 extends 80 m, 40 m and 6 m. */
    expect_int("R2 3.9 phone", type_at(BP_PLAN_R2, 3900000), BP_ALL);
    expect_int("R2 7.25 phone", type_at(BP_PLAN_R2, 7250000), BP_ALL);
    expect_int("R2 40m CW", type_at(BP_PLAN_R2, 7035000), BP_CW);
    expect_int("R2 6m beacon", type_at(BP_PLAN_R2, 50050000), BP_BCN);
    expect_int("R2 6m to 54", type_at(BP_PLAN_R2, 53000000), BP_ALL);

    /* Region 3: 80 m ends at 3.9 (exclusive), 40 m at 7.2, phone from 7030. */
    expect_int("R3 3.8 phone", type_at(BP_PLAN_R3, 3800000), BP_ALL);
    expect_int("R3 3.9 is the end", type_at(BP_PLAN_R3, 3900000), -1);
    expect_int("R3 7.25 not allocated", type_at(BP_PLAN_R3, 7250000), -1);
    expect_int("R3 7.035 phone", type_at(BP_PLAN_R3, 7035000), BP_ALL);
    expect_int("R3 30m gap left for the band list", type_at(BP_PLAN_R3, 10105000), -1);
    expect_int("R3 6m to 54", type_at(BP_PLAN_R3, 53000000), BP_ALL);

    /* USA: Extra phone edges, data windows, 60 m channels, 6 m to 54. */
    expect_int("USA 20m phone", type_at(BP_PLAN_USA, 14160000), BP_ALL);
    expect_int("USA 20m data", type_at(BP_PLAN_USA, 14074000), BP_DIG);
    expect_int("USA 20m still data at 14.149", type_at(BP_PLAN_USA, 14149000), BP_DIG);
    expect_int("USA FT8 40m", type_at(BP_PLAN_USA, 7074000), BP_DIG);
    expect_int("USA 40m phone at 7.125", type_at(BP_PLAN_USA, 7125000), BP_ALL);
    expect_int("USA 7.110 still CW", type_at(BP_PLAN_USA, 7110000), BP_CW);
    expect_int("USA 7.075 not phone", type_at(BP_PLAN_USA, 7075000), BP_DIG);
    expect_int("USA 40m CW", type_at(BP_PLAN_USA, 7035000), BP_CW);
    expect_int("USA 75m phone", type_at(BP_PLAN_USA, 3600000), BP_ALL);
    expect_int("USA FT8 80m", type_at(BP_PLAN_USA, 3573000), BP_DIG);
    expect_int("USA 80m CW", type_at(BP_PLAN_USA, 3500000), BP_CW);
    expect_int("USA 60m channel", type_at(BP_PLAN_USA, 5332000), BP_ALL);
    expect_int("USA 60m gap", type_at(BP_PLAN_USA, 5350000), -1);
    expect_int("USA 60m segment CW", type_at(BP_PLAN_USA, 5352000), BP_CW);
    expect_int("USA 6m to 54", type_at(BP_PLAN_USA, 53000000), BP_ALL);
    expect_int("USA 6m beacon", type_at(BP_PLAN_USA, 50070000), BP_BCN);

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

    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("band plan data ok\n");
    return 0;
}
