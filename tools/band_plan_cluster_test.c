/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  The cluster's mode guess with and without the strip's hook. No LVGL: spots.c is linked
 *  alone, and the hook is a stand-in for band_plan_type.
 *
 *    gcc -std=c11 -Wall -Wextra -Werror -I src -o /tmp/band_plan_cluster_test \
 *        src/cluster/spots.c tools/band_plan_cluster_test.c
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "cluster/spots.h"
#include "band_plan_data.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_mode(const char *what, int32_t hz, cluster_mode_t want) {
    cluster_spot_t s;

    memset(&s, 0, sizeof(s));
    s.freq_hz = hz;
    if (cluster_guess_mode(&s) != want) {
        fprintf(stderr, "FAIL %s at %d: got %d, want %d\n", what, hz, (int)cluster_guess_mode(&s), (int)want);
        fails++;
    }
}

/* A USA-shaped answer at two frequencies, and nothing elsewhere. */
static int usa_stub(int32_t hz) {
    if (hz >= 7125000 && hz < 7300000) {
        return BP_ALL;
    }
    if (hz >= 7000000 && hz < 7040000) {
        return BP_CW;
    }
    if (hz >= 29510000 && hz < 29700000) {
        return BP_ALL;
    }
    return -1;
}

int main(void) {
    /* No hook: the Region 1 edges compiled into spots.c. */
    cluster_set_band_plan(NULL);
    expect_mode("R1 fallback CW", 1810000, CLUSTER_MODE_CW);
    expect_mode("R1 fallback phone", 7100000, CLUSTER_MODE_LSB);
    expect_mode("R1 30m", 10120000, CLUSTER_MODE_CW);
    expect_mode("digi dial before the plan", 14074000, CLUSTER_MODE_DIGI);
    expect_mode("10m FM", 29520000, CLUSTER_MODE_FM);

    cluster_set_band_plan(usa_stub);
    expect_mode("hook CW", 7035000, CLUSTER_MODE_CW);
    /* 7.125 is phone on the USA plan. The old Region 1 edges would still have called 7.100 CW
     * and 7.125 LSB; the hook is what moves the edge. */
    expect_mode("hook phone", 7125000, CLUSTER_MODE_LSB);
    expect_mode("hook 10m FM inside phone", 29520000, CLUSTER_MODE_FM);
    /* Between segments the 30 m rule remains, so a plan with a gap there still guesses CW. */
    expect_mode("hook gap is 30m CW", 10120000, CLUSTER_MODE_CW);

    {
        cluster_spot_t s;

        memset(&s, 0, sizeof(s));
        s.freq_hz = 7125000;
        snprintf(s.comment, sizeof(s.comment), "CW");
        if (cluster_guess_mode(&s) != CLUSTER_MODE_CW) {
            fprintf(stderr, "FAIL comment over hook\n");
            fails++;
        }
    }

    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("band plan cluster guess ok\n");
    return 0;
}
