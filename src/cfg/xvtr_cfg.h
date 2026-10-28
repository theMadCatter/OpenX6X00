/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  The twelve transverter slots, stored in the existing `transverter` table
 *  (id, name, val). Ids 0 and 1 still carry the old from/to/shift rows, so a
 *  radio that only ever had the two-slot page keeps them. The new columns are
 *  more rows of the same table (lo, side, en, rx, pwr, cal, name), which an
 *  older build ignores.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#ifndef XVTR_CFG_H
#define XVTR_CFG_H

#include "../xvtr.h"

#ifdef __cplusplus
class Subject;
#else
typedef struct Subject Subject;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Read every slot. vhf_rx is nonzero on the X6200 (2 m / 70 cm start off).
 * Called once at startup, before the band clamps, and again when the web
 * configurator has written the table. */
void cfg_xvtr_load(int vhf_rx);
void cfg_xvtr_reload(void);

/* Copy slot `index` (0 .. XVTR_COUNT-1). Returns 0, or -1 when index is out of range. */
int cfg_xvtr_get(int index, xvtr_slot_t *out);

/* Replace slot `index`. Returns 0, or -1 when the row is rejected (range,
 * flags, or an enabled slot whose whole range falls outside the HF IF). */
int cfg_xvtr_set(int index, const xvtr_slot_t *in);

/* 1 and a copy of the slot when an enabled slice contains rf. */
int cfg_xvtr_for_rf(int64_t rf, xvtr_slot_t *out);

/* IF to send the radio. With no transverter, rf itself (the HF dial). */
int64_t cfg_rf_to_if(int64_t rf);

/* Nonzero when the slot covering rf is high-side (the sideband is inverted). */
int cfg_xvtr_inverted(int64_t rf);

/* Drive cap in watts, or a negative value when this frequency has no extra cap. */
float cfg_xvtr_pwr_cap_w(int64_t rf);

/* XVTR_USE_NONE / _RX / _TX over [lo, hi]. */
int cfg_xvtr_span_use(int64_t lo, int64_t hi);

/* Inclusive RF edges of each enabled slice, up to max. Returns how many. */
int cfg_xvtr_edges(int64_t *lo, int64_t *hi, int max);

/* Bumps when a slot is saved or the table is reloaded. The radio retunes. */
Subject *cfg_xvtr_changed(void);

#ifdef __cplusplus
}
#endif

#endif
