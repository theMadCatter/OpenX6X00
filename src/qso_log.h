/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BAND_OTHER  = 0,
    BAND_2M     = 2,
    BAND_3CM    = 3,     /* 10 GHz. Not a meter number the old log used */
    BAND_4M     = 4,
    BAND_6M     = 6,
    BAND_9CM    = 9,
    BAND_10M    = 10,
    BAND_12M    = 12,
    BAND_13CM   = 13,
    BAND_15M    = 15,
    BAND_17M    = 17,
    BAND_20M    = 20,
    BAND_23CM   = 23,
    BAND_30M    = 30,
    BAND_33CM   = 33,
    BAND_40M    = 40,
    BAND_60M    = 60,
    BAND_70CM   = 70,
    BAND_80M    = 80,
    BAND_125M   = 125,   /* 1.25 m */
    BAND_160M   = 160,
    BAND_6CM    = 600,   /* not 6: that is 6 m, and an old atoi("6cm") must not land here */
    BAND_125CM  = 1250,  /* 1.25 cm, 24 GHz */
} qso_log_band_t;


typedef enum {
    MODE_OTHER,
    MODE_SSB,
    MODE_AM,
    MODE_FM,
    MODE_CW,
    MODE_FT8,
    MODE_FT4,
    MODE_RTTY,
    MODE_JS8,   /* ADIF MFSK / JS8 */
    MODE_FT2,   /* ADIF MFSK / FT2 */
} qso_log_mode_t;

typedef struct {
    char local_call[32];
    char remote_call[32];
    time_t time;
    qso_log_mode_t mode;
    int rsts;
    int rstr;
    double freq_mhz; /* float loses about 1 kHz at 10 GHz */
    qso_log_band_t band;
    char name[64];
    char qth[64];
    char local_grid[8];
    char remote_grid[8];
} qso_log_record_t;


typedef enum {
    SEARCH_WORKED_NO,
    SEARCH_WORKED_YES,
    SEARCH_WORKED_SAME_MODE,
} qso_log_search_worked_t;


bool qso_log_init();

int qso_log_record_save(qso_log_record_t qso);

void qso_log_import_adif(const char *path);

/**
 * Create qso log recort struct.
 * Required params: `local_call`, `remote_call`, qso_time`, `mode`, `rsts`, `rstr`,  and `freq_mhz`
 */
qso_log_record_t qso_log_record_create(const char *local_call, const char *remote_call,
                                       time_t qso_time, qso_log_mode_t mode, int rsts, int rstr, uint64_t freq_hz,
                                       const char *name, const char *qth,
                                       const char *local_grid, const char *remote_grid);

/**
 * Search callsign in log.
 */
qso_log_search_worked_t qso_log_search_worked(const char *callsign, qso_log_mode_t mode, qso_log_band_t band);


qso_log_band_t qso_log_freq_to_band(uint64_t freq_hz);
/* Canonical ADIF BAND ("160m", "6cm", "1.25cm"), or "" for BAND_OTHER. */
const char *qso_log_band_adif(qso_log_band_t band);
/* Case-insensitive. "160M" is 160 m. "6cm" is not 6 m. A bare number is not accepted. */
qso_log_band_t qso_log_band_from_adif(const char *s);
