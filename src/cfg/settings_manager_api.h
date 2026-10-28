#pragma once

// C-compatible API for the SettingsManager-level operations (settings_manager.h):
// initialisation, deferred-write flushing, band/VFO switching, frequency-step
// cycling and hardware-frequency helpers. Parameter value accessors are in
// parameter_api.h.
// Copyright (c) 2026 Chris Fink KD0CQ

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the manager (loads global/band/mode params) and fill the extern
// globals in cfg_api.h. The caller owns the sqlite3 connection/table init
// (cfg_api_init is not handed a db handle). on_db_error is kept for signature
// compatibility; it is currently inert.
void cfg_api_init(void (*on_db_error)(const char *));

/* What the radio can do on a band or at a frequency */
typedef enum {
    BAND_USE_NONE = 0,      /* nothing: it cannot tune there */
    BAND_USE_RX,            /* receive only: the X6200's FM broadcast and airband, the WR12's 54 MHz-1 GHz */
    BAND_USE_TX,            /* receive and transmit: HF, a transverter with a shift */
} band_use_t;

/* What the radio can do somewhere in [start, stop] (Hz; start == stop for one frequency).
 * A transverter is judged on the slice that maps into the HF IF. */
band_use_t cfg_band_use(int64_t start, int64_t stop);
// True when freq is usable by the hardware: HF 0.1-55 MHz, a transverter slice,
// the X6200's receive ranges.
bool cfg_is_valid_hw_freq(int64_t freq);
/* Received but never transmitted on: the X6200's FM broadcast and airband, the WR12, an RX-only transverter */
bool cfg_is_rx_only_freq(int64_t freq);
/* Transmitted on: HF, a transverter that is not RX-only */
bool cfg_is_tx_freq(int64_t freq);
/* The band the radio is on is one it can use (band_use over its edges); true between bands */
bool cfg_band_usable(void);

// Persist all pending deferred writes immediately.
void cfg_api_flush_all(void);

// Start/stop the background deferred-save thread; wakes every ~3 s and calls
// flush_all(). Inert/no-op when the thread is already running / already stopped.
void cfg_api_start_flush_thread(void);
void cfg_api_stop_flush_thread(void);

// --- Band switching ---
// Load the next/previous band above/below current frequency.
void cfg_band_load_next(bool up);

/* TX power in W (p_pwr, 0.1..10), saved and applied as Settings does it */
void cfg_pwr_set(float watts);

/* The other VFO; with independent VFOs (p_indep_vfo) its own band comes with it */
void cfg_vfo_toggle(void);

/* The mode a frequency opens in: its sideband (LSB under 10 MHz, USB above and on 60 m), WFM and AM on the X6200's FM
 * broadcast and airband, NFM where only the WR12 receives */
int32_t cfg_default_mode(int64_t freq);

/* The active bands, lowest first: start and stop in Hz and the name of each, up to max; returns how many.
 * Edges are int64: 10 GHz and 24 GHz do not fit in 32 bits. */
int cfg_bands_active(int64_t *start, int64_t *stop, char (*names)[16], int max);
/* The same with the bands' ids (ids may be NULL) */
int cfg_bands_active_ids(int32_t *ids, int64_t *start, int64_t *stop, char (*names)[16], int max);

// Copy active VFO (freq, mode, agc, att, pre) to inactive VFO.
void cfg_band_vfo_copy(void);

// --- Step cycling ---
// Cycle freq_step through [10, 100, 500, 1000, 5000] Hz. Returns new step.
int32_t cfg_mode_change_freq_step(bool up);

#ifdef __cplusplus
} // extern "C"
#endif