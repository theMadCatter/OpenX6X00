/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "band_plan.h"

#include "cfg/cfg_api.h"
#include "cluster/spots.h"
#include "params/params.h"
#include "remote_server.h"
#include "styles.h"
#include "theme.h"

#include <pthread.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fonts/font_select.h"

/* VISION: the sizes of the other themes here (an overlay on VISION's shorter waterfall) */
#define NF(n) (theme_shape.vision ? font_down(&sony_##n, VISION_FONT_STEPS) : &sony_##n)

#define WIDTH   800
#define POOL    16      /* segments in view: a span is 100 kHz at most */
#define MAX_SEG 160     /* a plan of about fifty segments, and the bands added to the band list */
#define BANDS   48
#define OPA     LV_OPA_80     /* the waterfall shows through a little, the labels stay readable */

extern lv_font_t sony_18;


static const theme_color_t colors[] = { TC_bp_cw, TC_bp_dig, TC_bp_ssb, TC_bp_bcn };
/* Longest first: the first label that fits the segment is used */
static const char *const labels[][2] = {
    { "CW", NULL }, { "DIG", NULL }, { "ALL MODES / SSB", "SSB" }, { "BCN", NULL },
};

typedef struct {
    lv_obj_t *box;
    lv_obj_t *label;
} seg_t;

static seg_t   segs[POOL];
static bool    ready;
static int32_t span_center, span_half;

/* The plan in use: the table band_plan_table() returned and, where it has nothing, the active bands of the
 * band list (a band added on the web page, "60m CW" at 5.3 MHz, had no strip and no CW keyboard: issue #5).
 * Read from any thread (remote_server, the cluster's mode guess). */

/* Auto reads the station callsign. An explicit plan does not, so Region 1 stays the old strip. Before
 * cfg_api_init the pointer is NULL and the callsign is empty: that is Region 1, as a radio with no
 * callsign always was. */
static int selected_plan(void) {
    char call[32];
    int  sel = BP_PLAN_AUTO;

    if (cfg_band_plan_sel) {
        sel = param_i_get(cfg_band_plan_sel);
    }
    call[0] = '\0';
    if (sel == BP_PLAN_AUTO) {
        params_lock();
        snprintf(call, sizeof(call), "%s", params.callsign.x);
        params_unlock(NULL);
    }
    return band_plan_resolve(sel, call);
}

/* Extra when the setting is missing or out of range. Ignored unless the plan is USA. */
static int selected_class(void) {
    int cls = BP_CLASS_EXTRA;

    if (cfg_band_plan_class) {
        cls = param_i_get(cfg_band_plan_class);
    }
    if (cls < BP_CLASS_EXTRA || cls > BP_CLASS_NOVICE) {
        cls = BP_CLASS_EXTRA;
    }
    return cls;
}
static bp_seg_t        cur[MAX_SEG];
static int             ncur;
static pthread_mutex_t cur_lock = PTHREAD_MUTEX_INITIALIZER;

/* A band's own segment type, from its name as the presets are named ("40m CW", "20m SSB"); its last used mode
 * is no type: it changes with every mode change */
static bp_type_t type_from_name(const char *name) {
    if (strcasestr(name, "CW")) return BP_CW;
    if (strcasestr(name, "DIG") || strcasestr(name, "FT8") || strcasestr(name, "FT4") || strcasestr(name, "DATA")) return BP_DIG;
    if (strcasestr(name, "BCN") || strcasestr(name, "BEACON")) return BP_BCN;
    return BP_ALL;
}

static int by_start(const void *a, const void *b) {
    const bp_seg_t *x = a, *y = b;

    return x->start < y->start ? -1 : x->start > y->start;
}

void band_plan_reload(void) {
    static int32_t ids[BANDS], start[BANDS], stop[BANDS];
    static char    names[BANDS][16];
    static bp_seg_t list[MAX_SEG];
    const bp_seg_t *table = NULL;
    int             plan  = selected_plan();
    int             cls   = selected_class();
    int             nb    = cfg_bands_active_ids(ids, start, stop, names, BANDS);
    int             n     = band_plan_table(plan, cls, &table);

    /* A plan this build does not know returns nothing. Region 1 is the fallback inside
     * band_plan_resolve, so this is only a guard against copying past the array. */
    if (n > MAX_SEG) {
        n = MAX_SEG;
    }
    if (n > 0 && table) {
        memcpy(list, table, (size_t)n * sizeof(list[0]));
    } else {
        n = 0;
    }
    for (int i = 0; i < nb; i++) {
        /* Broadcast, the X6200's FM broadcast and airband, CB (ids 100-299 in sql/params.sql and migrations.c) are no
         * amateur bands; by their names too, so a user's band that got such an id keeps its strip */
        if (ids[i] >= 100 && ids[i] < 300
            && (strncasecmp(names[i], "Broadcast", 9) == 0 || strncasecmp(names[i], "CB", 2) == 0
                || strcasecmp(names[i], "FM broadcast") == 0 || strcasecmp(names[i], "Airband") == 0)) {
            continue;
        }
        int32_t   a = start[i], e = stop[i];
        bp_type_t t = type_from_name(names[i]);
        int       base = n;

        qsort(list, n, sizeof(list[0]), by_start);
        /* The parts of [a, e] no segment covers yet. On the USA plan a hole that is amateur
         * spectrum this class may not transmit on stays empty: the stock 20 m band must not
         * paint a Technician's strip. A band outside those allocations (one the operator added)
         * is still filled. */
        for (int k = 0; k < base && a < e && n < MAX_SEG; k++) {
            int32_t ps[8], pe[8];
            int     np, p;

            if (list[k].end <= a) continue;
            if (list[k].start >= e) break;
            if (list[k].start > a) {
                np = band_plan_keep_span(plan, cls, a, list[k].start, ps, pe, 8);
                for (p = 0; p < np && n < MAX_SEG; p++) list[n++] = (bp_seg_t){ps[p], pe[p], t};
            }
            a = list[k].end > a ? list[k].end : a;
        }
        if (a < e && n < MAX_SEG) {
            int32_t ps[8], pe[8];
            int     np = band_plan_keep_span(plan, cls, a, e, ps, pe, 8);
            int     p;

            for (p = 0; p < np && n < MAX_SEG; p++) list[n++] = (bp_seg_t){ps[p], pe[p], t};
        }
    }
    qsort(list, n, sizeof(list[0]), by_start);
    pthread_mutex_lock(&cur_lock);
    memcpy(cur, list, n * sizeof(list[0]));
    ncur = n;
    pthread_mutex_unlock(&cur_lock);
    /* After the lock: the panel copies the segments, and it must not run while cur_lock is held.
     * No panel client yet (and before remote_server_init) is a no-op. */
    remote_server_band_plan();
}

static void redraw(void) {
    int n = 0;

    if (!ready) {
        return;
    }
    if (span_half > 0 && param_i_get(cfg_band_plan)) {
        int32_t left = span_center - span_half;
        int64_t span = 2 * (int64_t)span_half;

        pthread_mutex_lock(&cur_lock);
        for (int i = 0; i < ncur && n < POOL; i++) {
            const bp_seg_t *p    = &cur[i];
            int64_t         a    = (int64_t)(p->start - left) * WIDTH / span;
            int64_t         b    = (int64_t)(p->end - left) * WIDTH / span;
            seg_t          *s    = &segs[n];
            const char     *text = NULL;

            if (b <= 0 || a >= WIDTH) {
                continue;
            }
            if (a < 0)     a = 0;
            if (b > WIDTH) b = WIDTH;
            if (b <= a)    b = a + 1;

            lv_obj_set_x(s->box, a);
            /* 1bit: a 1 px paper gap between segments, which no longer differ in colour */
            lv_obj_set_width(s->box, theme_shape.mono ? LV_MAX(b - a - 1, 1) : b - a);
            lv_obj_set_style_bg_color(s->box, lv_color_hex(theme_hex(colors[p->type])), 0);

            /* No label when it does not fit the visible part */
            for (int k = 0; k < 2 && labels[p->type][k] && !text; k++) {
                const char *t = labels[p->type][k];

                if (lv_txt_get_width(t, strlen(t), NF(16), 0, LV_TEXT_FLAG_NONE) + 6 <= b - a) {
                    text = t;
                }
            }
            if (text) {
                lv_label_set_text_static(s->label, text);
                lv_obj_center(s->label);
                lv_obj_clear_flag(s->label, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s->label, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(s->box, LV_OBJ_FLAG_HIDDEN);
            n++;
        }
        pthread_mutex_unlock(&cur_lock);
    }
    for (; n < POOL; n++) {
        lv_obj_add_flag(segs[n].box, LV_OBJ_FLAG_HIDDEN);
    }
}

void band_plan_callsign_changed(void) {
    if (cfg_band_plan_sel && param_i_get(cfg_band_plan_sel) == BP_PLAN_AUTO) {
        band_plan_reload();
        band_plan_refresh();
    }
}

void band_plan_init(lv_obj_t *parent, lv_coord_t y) {
    band_plan_reload();
    /* The cluster guesses a mode without including this file (it has no LVGL). The hook reads the
     * plan just loaded. A theme rebuild calls init again; pointing the hook here once more is the
     * same function. Before this, and in a unit test, the cluster keeps its Region 1 CW edges. */
    cluster_set_band_plan(band_plan_type);
    for (int i = 0; i < POOL; i++) {
        lv_obj_t *b = lv_obj_create(parent);

        lv_obj_remove_style_all(b);
        lv_obj_set_y(b, y);
        lv_obj_set_height(b, BAND_PLAN_H);
        lv_obj_set_style_bg_opa(b, OPA, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        segs[i].box = b;

        lv_obj_t *l = lv_label_create(b);

        lv_obj_set_style_text_font(l, NF(16), 0);
        lv_obj_set_style_text_color(l, TC(bp_text), 0);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        segs[i].label = l;
    }
    ready = true;
    redraw();
}

void band_plan_set_y(lv_coord_t y) {
    for (int i = 0; i < POOL; i++) {
        if (segs[i].box) {
            lv_obj_set_y(segs[i].box, y);
        }
    }
}

void band_plan_update(int32_t center, int32_t half_width) {
    if (center == span_center && half_width == span_half) {
        return;
    }
    span_center = center;
    span_half   = half_width;
    redraw();
}

void band_plan_refresh(void) {
    redraw();
}

bool band_plan_segment(int i, int32_t *start, int32_t *end, int *type) {
    bool ok;

    pthread_mutex_lock(&cur_lock);
    ok = i >= 0 && i < ncur;
    if (ok) {
        *start = cur[i].start;
        *end   = cur[i].end;
        *type  = cur[i].type;
    }
    pthread_mutex_unlock(&cur_lock);
    return ok;
}

int band_plan_type(int32_t freq) {
    int type = -1;

    pthread_mutex_lock(&cur_lock);
    for (int i = 0; i < ncur; i++) {
        if (freq >= cur[i].start && freq < cur[i].end) {
            type = cur[i].type;
            break;
        }
    }
    pthread_mutex_unlock(&cur_lock);
    return type;
}

bool band_plan_is_cw(int32_t freq) {
    return band_plan_type(freq) == BP_CW;
}

bool band_plan_tx_warning(int32_t freq) {
    return band_plan_tx_warn(selected_plan(), selected_class(), freq) != 0;
}
