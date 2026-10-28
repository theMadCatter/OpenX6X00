/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Load and save the transverter slots.
 *
 *  The table is the one the two-slot page already used: (id, name, val).
 *  A radio that has `from`, `to` and `shift` for ids 0 and 1 keeps them.
 *  `en` missing and `shift` present means the old rule: a shift of 0 is off.
 *  `lo` missing is copied from `shift` when that shift is not zero.
 *  `shift` is still written for a low-side LO the old page can represent
 *  (0 .. 500 MHz), so a downgrade of 2 m / 70 cm still tunes. A high-side
 *  slot, and any LO the old int32 page cannot store, writes shift 0 so the
 *  old firmware leaves the transverter off instead of transmitting on a
 *  truncated IF. The calibration offset is not folded into that shift.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "xvtr_cfg.h"

#include "db.h"
#include "subject.h"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

constexpr int64_t kLegacyShiftMax = 500000000LL; /* the old page's upper cap, Hz */

xvtr_slot_t        slots[XVTR_COUNT];
std::mutex         mu;
SubjectT<int32_t>  generation{0};
int                vhf_rx_flag;

void copy_name(char *dst, const char *src) {
    size_t n = 0;
    if (!src) {
        src = "";
    }
    while (src[n] && n + 1 < XVTR_NAME_LEN) {
        unsigned char c = static_cast<unsigned char>(src[n]);
        dst[n]          = (c >= 32 && c < 127) ? static_cast<char>(c) : ' ';
        n++;
    }
    dst[n] = '\0';
}

bool load_i64(int id, const char *name, int64_t *out) {
    ParamLoadResult<int64_t> r = TransverterTable::Load<int64_t>(id, name);
    if (r.rc != SUCCESS) {
        return false;
    }
    *out = r.value;
    return true;
}

bool load_i32(int id, const char *name, int32_t *out) {
    ParamLoadResult<int32_t> r = TransverterTable::Load<int32_t>(id, name);
    if (r.rc != SUCCESS) {
        return false;
    }
    *out = r.value;
    return true;
}

void save_i64(int id, const char *name, int64_t value) {
    TransverterTable::Save<int64_t>(id, name, value);
}

void save_slot(int id, const xvtr_slot_t &s) {
    int64_t legacy = 0;
    if (s.enabled && !s.high_side && s.lo_hz > 0 && s.lo_hz <= kLegacyShiftMax) {
        legacy = s.lo_hz;
    }
    save_i64(id, "from", s.rf_from);
    save_i64(id, "to", s.rf_to);
    save_i64(id, "lo", s.lo_hz);
    save_i64(id, "shift", legacy);
    save_i64(id, "side", s.high_side ? 1 : 0);
    save_i64(id, "en", s.enabled ? 1 : 0);
    save_i64(id, "rx", s.rx_only ? 1 : 0);
    save_i64(id, "pwr", s.pwr_max_dw);
    save_i64(id, "cal", s.cal_hz);
    TransverterTable::Save<std::string>(id, "name", s.name);
}

/* A row the operator can turn on: edges inside the RF window, and at least
 * one hertz of that range maps into the HF IF. An off row may be empty. */
bool acceptable(const xvtr_slot_t &s) {
    if (s.high_side < 0 || s.high_side > 1 || s.enabled < 0 || s.enabled > 1 || s.rx_only < 0 || s.rx_only > 1) {
        return false;
    }
    if (s.pwr_max_dw < 0 || s.pwr_max_dw > XVTR_PWR_MAX_DW) {
        return false;
    }
    if (s.cal_hz < -XVTR_CAL_MAX_HZ || s.cal_hz > XVTR_CAL_MAX_HZ) {
        return false;
    }
    if (!s.enabled) {
        if (s.rf_from == 0 && s.rf_to == 0) {
            return true;
        }
    }
    if (s.rf_from < XVTR_RF_MIN_HZ || s.rf_to > XVTR_RF_MAX_HZ || s.rf_from >= s.rf_to) {
        return false;
    }
    if (s.lo_hz < 0 || s.lo_hz > XVTR_RF_MAX_HZ + XVTR_IF_MAX_HZ) {
        return false;
    }
    if (s.enabled) {
        int64_t a, b;
        if (!xvtr_slice(&s, &a, &b)) {
            return false;
        }
    }
    return true;
}

void load_one(int id) {
    xvtr_slot_t s;
    int64_t     from = 0, to = 0, lo = 0, shift = 0, cal = 0;
    int32_t     en = 0, side = 0, rx = 0, pwr = 0;
    bool        have_from, have_to, have_lo, have_shift, have_en, have_side, have_rx, have_pwr, have_cal;

    xvtr_preset(id, vhf_rx_flag, &s);
    have_from  = load_i64(id, "from", &from);
    have_to    = load_i64(id, "to", &to);
    have_lo    = load_i64(id, "lo", &lo);
    have_shift = load_i64(id, "shift", &shift);
    have_en    = load_i32(id, "en", &en);
    have_side  = load_i32(id, "side", &side);
    have_rx    = load_i32(id, "rx", &rx);
    have_pwr   = load_i32(id, "pwr", &pwr);
    have_cal   = load_i64(id, "cal", &cal);

    if (have_from) {
        s.rf_from = from;
    }
    if (have_to) {
        s.rf_to = to;
    }
    if (have_lo) {
        s.lo_hz = lo;
    } else if (have_shift && shift != 0) {
        s.lo_hz = shift;
    }
    if (have_en) {
        s.enabled = en ? 1 : 0;
    } else if (have_shift) {
        /* The old page: shift 0 is off, any other shift is on. */
        s.enabled = shift != 0 ? 1 : 0;
    }
    if (have_side) {
        s.high_side = side ? 1 : 0;
    }
    if (have_rx) {
        s.rx_only = rx ? 1 : 0;
    }
    if (have_pwr) {
        s.pwr_max_dw = pwr < 0 ? 0 : (pwr > XVTR_PWR_MAX_DW ? XVTR_PWR_MAX_DW : pwr);
    }
    if (have_cal) {
        s.cal_hz = cal;
    }

    ParamLoadResult<std::string> name = TransverterTable::Load<std::string>(id, "name");
    if (name.rc == SUCCESS && !name.value.empty()) {
        copy_name(s.name, name.value.c_str());
    }
    slots[id] = s;
}

void load_all() {
    for (int i = 0; i < XVTR_COUNT; i++) {
        load_one(i);
    }
}

} // namespace

void cfg_xvtr_load(int vhf_rx) {
    std::lock_guard<std::mutex> lock(mu);
    vhf_rx_flag = vhf_rx ? 1 : 0;
    load_all();
    /* The resolved slots go back to the table, so the web page and a later
     * boot see the same rows the radio is using. A shift the old page stored
     * is kept (load_one) and written again for a low-side LO it can represent. */
    for (int i = 0; i < XVTR_COUNT; i++) {
        save_slot(i, slots[i]);
    }
}

void cfg_xvtr_reload(void) {
    {
        std::lock_guard<std::mutex> lock(mu);
        load_all();
    }
    generation.set(generation.get() + 1);
}

int cfg_xvtr_get(int index, xvtr_slot_t *out) {
    if (index < 0 || index >= XVTR_COUNT || !out) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mu);
    *out = slots[index];
    return 0;
}

int cfg_xvtr_set(int index, const xvtr_slot_t *in) {
    if (index < 0 || index >= XVTR_COUNT || !in) {
        return -1;
    }
    xvtr_slot_t s = *in;
    copy_name(s.name, in->name);
    if (!s.name[0]) {
        xvtr_slot_t preset;
        xvtr_preset(index, vhf_rx_flag, &preset);
        copy_name(s.name, preset.name[0] ? preset.name : "XVTR");
    }
    if (!acceptable(s)) {
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(mu);
        slots[index] = s;
        save_slot(index, s);
    }
    generation.set(generation.get() + 1);
    return 0;
}

int cfg_xvtr_for_rf(int64_t rf, xvtr_slot_t *out) {
    std::lock_guard<std::mutex> lock(mu);
    const xvtr_slot_t          *s = xvtr_for_rf(slots, XVTR_COUNT, rf);
    if (!s) {
        return 0;
    }
    if (out) {
        *out = *s;
    }
    return 1;
}

int64_t cfg_rf_to_if(int64_t rf) {
    xvtr_slot_t s;
    if (!cfg_xvtr_for_rf(rf, &s)) {
        return rf;
    }
    return xvtr_if_hz(&s, rf);
}

int cfg_xvtr_inverted(int64_t rf) {
    xvtr_slot_t s;
    return cfg_xvtr_for_rf(rf, &s) && s.high_side;
}

float cfg_xvtr_pwr_cap_w(int64_t rf) {
    xvtr_slot_t s;
    if (!cfg_xvtr_for_rf(rf, &s) || s.pwr_max_dw <= 0) {
        return -1.f;
    }
    return static_cast<float>(s.pwr_max_dw) / 10.f;
}

int cfg_xvtr_span_use(int64_t lo, int64_t hi) {
    std::lock_guard<std::mutex> lock(mu);
    return xvtr_span_use(slots, XVTR_COUNT, lo, hi);
}

int cfg_xvtr_edges(int64_t *lo, int64_t *hi, int max) {
    std::lock_guard<std::mutex> lock(mu);
    int                         n = 0;
    for (int i = 0; i < XVTR_COUNT && n < max; i++) {
        int64_t a, b;
        if (xvtr_slice(&slots[i], &a, &b)) {
            lo[n] = a;
            hi[n] = b;
            n++;
        }
    }
    return n;
}

Subject *cfg_xvtr_changed(void) {
    return &generation;
}
