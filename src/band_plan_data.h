/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Band plan tables and the callsign that picks one. No LVGL: the strip in band_plan.c draws
 *  whatever this selects, and a host test can compile this file on its own.
 *
 *  Before this, the radio had one table, IARU Region 1, compiled into the strip. Region 2,
 *  Region 3 and a USA plan are further tables in the same shape, so another plan is another
 *  array and one more value of the setting. The strip's colours and "mode from band plan"
 *  only know four segment kinds, so each published plan is collapsed onto those.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The four colours of the strip, and the only kinds "mode from band plan" understands.
 * CW and all-modes move the mode; digital and beacon do not. Do not reorder: the browser
 * panel stores the number. */
typedef enum {
    BP_CW = 0,
    BP_DIG,
    BP_ALL,
    BP_BCN
} bp_type_t;

/* One segment. end is exclusive, so a frequency on the boundary belongs to the next segment. */
typedef struct {
    int32_t   start;
    int32_t   end;
    bp_type_t type;
} bp_seg_t;

/* The setting. 0 is Auto. The numbers are the order of the Settings dropdown and of the
 * web configurator's list; a new plan goes on the end and the setting's maximum moves with it. */
enum {
    BP_PLAN_AUTO = 0,
    BP_PLAN_R1   = 1,
    BP_PLAN_R2   = 2,
    BP_PLAN_R3   = 3,
    BP_PLAN_USA  = 4
};

/* The plan a callsign selects: USA for K, N, W and AA-AL, otherwise the ITU region of the
 * prefix, and Region 1 when the callsign is empty or nothing in the table matches. A portable
 * designator (P, M, MM, AM, QRP, A, R, or a single digit) is ignored; "KH6/W1AW" and
 * "W1AW/KH6" are both the place, not the home call. */
int band_plan_from_call(const char *call);

/* sel is the setting. Auto asks the callsign; an explicit plan is returned as it is; a value
 * this build does not know is Region 1, the plan the radio showed before there was a choice. */
int band_plan_resolve(int sel, const char *call);

/* The segments of a resolved plan (never Auto). False ids return 0 and leave *out alone.
 * The pointer is to a static table and stays valid. */
int band_plan_table(int plan, const bp_seg_t **out);

#ifdef __cplusplus
}
#endif
