/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Transverter conversion. The dial, the log and the band keys stay on the
 *  true RF frequency. The radio itself is only ever sent an IF inside its
 *  native range (see XVTR_IF_MIN_HZ / XVTR_IF_MAX_HZ, the same window as
 *  SettingsManager::HF_MIN_FREQ / HF_MAX_FREQ).
 *
 *  A low-side LO mixes as RF = LO + IF. A high-side LO mixes as RF = LO - IF
 *  and inverts the sideband; the inversion is applied at the hardware
 *  boundary, not to the mode the operator sees.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#ifndef XVTR_H
#define XVTR_H

#include <stddef.h>
#include <stdint.h>

/* C linkage. xvtr_cfg.cpp calls these from C++; xvtr.c defines them as C. */
#ifdef __cplusplus
extern "C" {
#endif

#define XVTR_COUNT          12
#define XVTR_NAME_LEN       16
/* Operator RF, Hz. 50 MHz is the bottom of a transverter (6 m is native).
 * 30 GHz leaves headroom past the 24 GHz / 1.25 cm band. */
#define XVTR_RF_MIN_HZ      50000000LL
#define XVTR_RF_MAX_HZ      30000000000LL
/* IF the baseband can be asked for. Must match the HF window. */
#define XVTR_IF_MIN_HZ      100000LL
#define XVTR_IF_MAX_HZ      55000000LL
/* LO error the operator can dial, Hz. Ten megahertz either way. */
#define XVTR_CAL_MAX_HZ     10000000LL
/* Drive cap in tenths of a watt. 0 means the radio's own power. 100 is 10.0 W. */
#define XVTR_PWR_MAX_DW     100
/* Above this the IF is not sent to the radio (the control word is 32-bit). */
#define XVTR_IF_HW_MAX_HZ   1500000000LL

/* What a span of RF can do through a transverter. Same order as band_use_t. */
#define XVTR_USE_NONE       0
#define XVTR_USE_RX         1
#define XVTR_USE_TX         2

typedef struct {
    char    name[XVTR_NAME_LEN]; /* short label, NUL terminated, 15 characters at most */
    int64_t rf_from;             /* covered RF, Hz, inclusive */
    int64_t rf_to;
    int64_t lo_hz;               /* local oscillator, Hz, before the calibration offset */
    int64_t cal_hz;              /* LO error, Hz; added to lo_hz. May be negative */
    int     high_side;           /* 0: RF = LO + IF. 1: RF = LO - IF, spectrum inverted */
    int     enabled;
    int     rx_only;             /* receive, no transmit, even inside the slice */
    int     pwr_max_dw;          /* 0: no extra cap. Else tenths of a watt, at most XVTR_PWR_MAX_DW */
} xvtr_slot_t;

/* Factory row. Slots 0 and 1 (2 m, 70 cm) start enabled only when vhf_rx is 0
 * (the X6100). The X6200 receives those ranges on the WR12, so they start off.
 * Slots 2..9 are the microwave presets, off. Slots 10 and 11 are empty spares. */
void xvtr_preset(int index, int vhf_rx, xvtr_slot_t *out);

/* LO actually used: lo_hz + cal_hz. */
int64_t xvtr_lo(const xvtr_slot_t *s);

/* RF that maps into the HF IF window, intersected with [rf_from, rf_to].
 * Returns 1 and writes the inclusive edges when that intersection is nonempty. */
int xvtr_slice(const xvtr_slot_t *s, int64_t *rf_lo, int64_t *rf_hi);

/* IF, Hz, for an RF on this slot. The caller has already decided the slot applies. */
int64_t xvtr_if_hz(const xvtr_slot_t *s, int64_t rf);

/* First enabled slot whose tunable slice overlaps [lo, hi]. RX-only is
 * XVTR_USE_RX, otherwise XVTR_USE_TX. XVTR_USE_NONE when nothing overlaps.
 * A lower index wins when two slices overlap. */
int xvtr_span_use(const xvtr_slot_t *slots, int n, int64_t lo, int64_t hi);

/* First enabled slot whose tunable slice contains rf, or NULL. */
const xvtr_slot_t *xvtr_for_rf(const xvtr_slot_t *slots, int n, int64_t rf);

/* ADIF BAND for an RF frequency: "160m", "2m", "70cm", "6cm", "3cm", "1.25cm",
 * or "" when the frequency is outside every listed range. One table, so the
 * log, the ADIF writer and the import agree. "6cm" is not "6m". */
const char *xvtr_adif_band(int64_t hz);

#ifdef __cplusplus
}
#endif

#endif
