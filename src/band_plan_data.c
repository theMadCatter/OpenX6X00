/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  The band plans, and which one a callsign means.
 *
 *  The strip used to be a single IARU Region 1 table in band_plan.c. That table is still here,
 *  unchanged, so a station on Region 1 looks as it did. Region 2, Region 3 and the USA plan
 *  were added beside it. Each published plan has more detail than four colours can show
 *  (contest preferred, QRP centres, emergency frequencies). Adjacent ranges that fall on the
 *  same colour are one segment. "Mode from band plan" turns the knob to CW in a CW segment
 *  and to the band's sideband in an all-modes segment, and leaves the mode alone in a digital
 *  or beacon segment, which is why a data window is marked digital even where CW is also legal.
 *
 *  Sources, read for this table:
 *    Region 1: the table the firmware already shipped (IARU Region 1, 160 m to 6 m, 60 m as
 *      the WRC-15 allocation). It was not redrawn.
 *    Region 2: IARU Region 2 band plan, September 2020
 *      (https://www.iaru-r2.org/en/reference/band-plans/, the HF and 6 m sections).
 *    Region 3: IARU Region 3 band plan, document R3-004 Rev. 2, adopted November 2024
 *      (https://www.iaru-r3.org/wp-content/uploads/2025/01/R3-004-Band-Plans-IARU-Region-3.pdf).
 *    USA: 47 CFR 97.301(b) and 97.305(c) as on the eCFR on 28 October 2026, for an Amateur
 *      Extra control operator in the continental United States. General, Advanced, Technician
 *      and Novice are narrower (97.301(c)-(e)); one strip cannot show every class. Where the
 *      FCC authorizes phone on only part of a band, that edge is the segment. Where it
 *      authorizes every mode on the whole band (160 m), the split is the on-air one, because
 *      otherwise the strip would be one colour and the mode would follow it. Noted per band.
 *    Callsign prefixes: ITU Radio Regulations, Article 5 (the three Regions) and Appendix 42
 *      (the call sign series). A series that is not in the table is treated as Region 1.
 *
 *  Copyright (c) 2026 Chris Fink KD0CQ
 */

#include "band_plan_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- Region 1 */

/* IARU Region 1, adjacent ranges of the same type merged. Kept byte for byte: an upgrade that
 * stays on Region 1 must not move a colour. */
static const bp_seg_t plan_r1[] = {
    {  1810000,  1838000, BP_CW  }, {  1838000,  1843000, BP_DIG }, {  1843000,  2000000, BP_ALL },
    {  3500000,  3570000, BP_CW  }, {  3570000,  3600000, BP_DIG }, {  3600000,  3800000, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },  /* WRC-15 */
    {  7000000,  7040000, BP_CW  }, {  7040000,  7050000, BP_DIG }, {  7050000,  7200000, BP_ALL },
    { 10100000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    { 14000000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14112000, BP_DIG }, { 14112000, 14350000, BP_ALL },
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18120000, BP_DIG }, { 18120000, 18168000, BP_ALL },
    { 21000000, 21070000, BP_CW  }, { 21070000, 21120000, BP_DIG }, { 21120000, 21149000, BP_CW  },
    { 21149000, 21151000, BP_BCN }, { 21151000, 21450000, BP_ALL },
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28190000, BP_DIG }, { 28190000, 28225000, BP_BCN },
    { 28225000, 29700000, BP_ALL },
    { 50000000, 50100000, BP_CW  }, { 50100000, 50300000, BP_ALL }, { 50300000, 50400000, BP_DIG },
    { 50400000, 50500000, BP_BCN }, { 50500000, 52000000, BP_ALL },
};

/* ---------------------------------------------------------------- Region 2 */

/* IARU Region 2, September 2020, collapsed the same way. 160 m starts at 1.8, 80 m runs to
 * 4.0, 40 m to 7.3 and 6 m to 54, which is where this plan differs from Region 1 on the air.
 * The PDF prints 17 m "18095-15105"; that is 18095-18105. 21.120-21.149 is 500 Hz, not phone,
 * so it stays CW, as in the Region 1 table. The ACDS slice just under the phone (14.101,
 * 18.111) stays digital, so the mode is not forced to SSB there. */
static const bp_seg_t plan_r2[] = {
    {  1800000,  1810000, BP_DIG }, {  1810000,  1839000, BP_CW  }, {  1839000,  1840000, BP_DIG },
    {  1840000,  2000000, BP_ALL },
    {  3500000,  3570000, BP_CW  }, {  3570000,  3600000, BP_DIG }, {  3600000,  4000000, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },
    {  7000000,  7040000, BP_CW  }, {  7040000,  7050000, BP_DIG }, {  7050000,  7300000, BP_ALL },
    { 10100000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    { 14000000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14112000, BP_DIG }, { 14112000, 14350000, BP_ALL },
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18120000, BP_DIG }, { 18120000, 18168000, BP_ALL },
    { 21000000, 21070000, BP_CW  }, { 21070000, 21120000, BP_DIG }, { 21120000, 21149000, BP_CW  },
    { 21149000, 21151000, BP_BCN }, { 21151000, 21450000, BP_ALL },
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28190000, BP_DIG }, { 28190000, 28225000, BP_BCN },
    { 28225000, 29700000, BP_ALL },
    { 50000000, 50100000, BP_BCN }, { 50100000, 54000000, BP_ALL },
};

/* ---------------------------------------------------------------- Region 3 */

/* IARU Region 3, November 2024. 7.2-7.3 was dropped from this plan because it is not an
 * amateur allocation in Region 3. Phone is in the 40 m plan from 7030, and 80 m stops at
 * 3.9. 30 m CW in the plan starts at 10110; 10100-10110 is left for the band list to fill.
 * Beacon guards are the ±500 Hz the plan names around the international beacons. */
static const bp_seg_t plan_r3[] = {
    {  1800000,  1830000, BP_CW  }, {  1830000,  1840000, BP_DIG }, {  1840000,  2000000, BP_ALL },
    {  3500000,  3535000, BP_CW  }, {  3535000,  3900000, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },
    {  7000000,  7030000, BP_CW  }, {  7030000,  7200000, BP_ALL },
    { 10110000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    { 14000000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14112000, BP_DIG }, { 14112000, 14350000, BP_ALL },
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18120000, BP_DIG }, { 18120000, 18168000, BP_ALL },
    { 21000000, 21070000, BP_CW  }, { 21070000, 21149000, BP_DIG }, { 21149000, 21151000, BP_BCN },
    { 21151000, 21450000, BP_ALL },
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28190000, BP_DIG }, { 28190000, 28225000, BP_BCN },
    { 28225000, 29700000, BP_ALL },
    { 50000000, 50020000, BP_CW  }, { 50020000, 50080000, BP_BCN }, { 50080000, 50100000, BP_CW  },
    { 50100000, 50300000, BP_ALL }, { 50300000, 50330000, BP_DIG }, { 50330000, 54000000, BP_ALL },
};

/* -------------------------------------------------------------------- USA */

/* FCC Part 97, continental United States, Region 2, read from the eCFR on 28 October 2026.
 * 97.301 is which frequencies the class may transmit on. 97.305 is which emissions, and
 * 97.305(a) allows CW on any frequency the operator is authorized. A data window inside a
 * RTTY/data segment is marked digital so FT8 does not flip the radio to CW. The window is
 * customary, and it sits where data is actually authorized.
 *
 * Extra is the default class: this first table is the one the radio drew before there was
 * a class. Advanced, General, Technician and Novice are further tables. A hole (General
 * has no 7.125-7.175) is left out, so the strip has no colour there. Nothing in these
 * tables stops the receiver, and nothing refuses a transmission: MARS, CAP and emergency
 * use are the operator's, and the colour is only a warning. */

static const bp_seg_t plan_usa[] = {
    /* 160 m. 97.305 authorizes phone, image, RTTY and data on the entire band, so the FCC
     * draws no phone edge. The split is the on-air one: digital 1.800-1.810, CW to 1.840,
     * phone above. Without it the strip would be one colour. */
    {  1800000,  1810000, BP_DIG }, {  1810000,  1840000, BP_CW  }, {  1840000,  2000000, BP_ALL },
    /* 80 m is 3.5-3.6 (RTTY/data) and 75 m is 3.6-4.0 (phone) for Extra in Region 2.
     * General phone starts at 3.8, Advanced at 3.7. 3.570-3.600 holds FT8 (3.573). */
    {  3500000,  3570000, BP_CW  }, {  3570000,  3600000, BP_DIG }, {  3600000,  4000000, BP_ALL },
    /* 60 m, eCFR 97.305 on 28 October 2026: channel centres 5.332, 5.348, 5.373 and 5.405 MHz
     * (USB dial 1.5 kHz lower, 2.8 kHz wide, 97.307(f)(14)) and the segment 5.3515-5.3665.
     * The old channel at 5.3585 sits inside that segment. 60 m changed during 2026; confirm
     * the current rule before relying on a channel. */
    {  5330500,  5333300, BP_ALL }, {  5346500,  5349300, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },
    {  5371500,  5374300, BP_ALL }, {  5403500,  5406300, BP_ALL },
    /* 40 m. Continental Extra phone starts at 7.125. Phone at 7.075-7.100 is only for FCC
     * stations in ITU Regions 1 and 3, and in Region 2 west of 130 degrees west or south of
     * 20 degrees north (97.307(f)(11)); it is not the continental segment. 7.040-7.100 is the
     * data window (FT8 7.074) inside the RTTY/data range 7.000-7.125. */
    {  7000000,  7040000, BP_CW  }, {  7040000,  7100000, BP_DIG },
    {  7100000,  7125000, BP_CW  }, {  7125000,  7300000, BP_ALL },
    /* 30 m: RTTY/data on the whole band, no phone. */
    { 10100000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    /* 20 m. Data to 14.150, Extra phone from 14.150 (General phone is 14.225).
     * 14.099-14.101 is the IARU beacon guard stations keep clear. Automatic beacons in
     * 97.203 do not include 20 m, so this sliver is practice, not a regulatory exclusive. */
    { 14000000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14150000, BP_DIG }, { 14150000, 14350000, BP_ALL },
    /* 17 m. FCC phone starts at 18.110. The beacon guard is 18.109-18.111, so the phone
     * colour starts 1 kHz above the regulatory edge. */
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18168000, BP_ALL },
    /* 15 m. Data to 21.200, Extra phone from 21.200 (Advanced 21.225, General 21.275). */
    { 21000000, 21070000, BP_CW  }, { 21070000, 21149000, BP_DIG }, { 21149000, 21151000, BP_BCN },
    { 21151000, 21200000, BP_DIG }, { 21200000, 21450000, BP_ALL },
    /* 12 m. Phone from 24.930. Beacon guard 24.929-24.931, the same 1 kHz overlap as 17 m. */
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    /* 10 m. Data to 28.300, phone from 28.300. 28.200-28.300 is the automatic-beacon segment
     * of 97.203. Novice and Technician phone stops at 28.500. */
    { 28000000, 28070000, BP_CW  }, { 28070000, 28200000, BP_DIG }, { 28200000, 28300000, BP_BCN },
    { 28300000, 29700000, BP_ALL },
    /* 6 m. 97.305(c) starts phone and data at 50.1, so 50.0-50.1 is CW. Beacons 50.06-50.08
     * are 97.203. 50.300-50.350 is the FT8 window inside the phone segment. */
    { 50000000, 50060000, BP_CW  }, { 50060000, 50080000, BP_BCN }, { 50080000, 50100000, BP_CW  },
    { 50100000, 50300000, BP_ALL },     { 50300000, 50350000, BP_DIG }, { 50350000, 54000000, BP_ALL },
};

/* Advanced, 97.301(c). The bottom 25 kHz of 80, 40, 20 and 15 m is Extra only.
 * 3.600-3.700, 14.150-14.175 and 21.200-21.225 are Extra only as well: they are
 * not in the Advanced rows of 97.301(c). Phone on 40 m starts at 7.125, the same
 * edge as Extra (97.305). 160, 60, 30, 17, 12, 10 and 6 m match Extra. */
static const bp_seg_t plan_usa_adv[] = {
    {  1800000,  1810000, BP_DIG }, {  1810000,  1840000, BP_CW  }, {  1840000,  2000000, BP_ALL },
    {  3525000,  3570000, BP_CW  }, {  3570000,  3600000, BP_DIG },
    {  3700000,  4000000, BP_ALL },
    {  5330500,  5333300, BP_ALL }, {  5346500,  5349300, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },
    {  5371500,  5374300, BP_ALL }, {  5403500,  5406300, BP_ALL },
    {  7025000,  7040000, BP_CW  }, {  7040000,  7100000, BP_DIG },
    {  7100000,  7125000, BP_CW  }, {  7125000,  7300000, BP_ALL },
    { 10100000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    { 14025000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14150000, BP_DIG },
    { 14175000, 14350000, BP_ALL },
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18168000, BP_ALL },
    { 21025000, 21070000, BP_CW  }, { 21070000, 21149000, BP_DIG }, { 21149000, 21151000, BP_BCN },
    { 21151000, 21200000, BP_DIG },
    { 21225000, 21450000, BP_ALL },
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28200000, BP_DIG }, { 28200000, 28300000, BP_BCN },
    { 28300000, 29700000, BP_ALL },
    { 50000000, 50060000, BP_CW  }, { 50060000, 50080000, BP_BCN }, { 50080000, 50100000, BP_CW  },
    { 50100000, 50300000, BP_ALL }, { 50300000, 50350000, BP_DIG }, { 50350000, 54000000, BP_ALL },
};

/* General, 97.301(d). Phone: 3.800, 7.175, 14.225, 21.275, and 28.300 as for Extra.
 * 7.125-7.175, 14.150-14.225 and 21.200-21.275 are not General frequencies at all.
 * 3.600-3.800 is not either. 160, 60, 30, 17, 12, 10 and 6 m match Extra. */
static const bp_seg_t plan_usa_gen[] = {
    {  1800000,  1810000, BP_DIG }, {  1810000,  1840000, BP_CW  }, {  1840000,  2000000, BP_ALL },
    {  3525000,  3570000, BP_CW  }, {  3570000,  3600000, BP_DIG },
    {  3800000,  4000000, BP_ALL },
    {  5330500,  5333300, BP_ALL }, {  5346500,  5349300, BP_ALL },
    {  5351500,  5354000, BP_CW  }, {  5354000,  5366000, BP_ALL }, {  5366000,  5366500, BP_DIG },
    {  5371500,  5374300, BP_ALL }, {  5403500,  5406300, BP_ALL },
    {  7025000,  7040000, BP_CW  }, {  7040000,  7100000, BP_DIG },
    {  7100000,  7125000, BP_CW  },
    {  7175000,  7300000, BP_ALL },
    { 10100000, 10130000, BP_CW  }, { 10130000, 10150000, BP_DIG },
    { 14025000, 14070000, BP_CW  }, { 14070000, 14099000, BP_DIG }, { 14099000, 14101000, BP_BCN },
    { 14101000, 14150000, BP_DIG },
    { 14225000, 14350000, BP_ALL },
    { 18068000, 18095000, BP_CW  }, { 18095000, 18109000, BP_DIG }, { 18109000, 18111000, BP_BCN },
    { 18111000, 18168000, BP_ALL },
    { 21025000, 21070000, BP_CW  }, { 21070000, 21149000, BP_DIG }, { 21149000, 21151000, BP_BCN },
    { 21151000, 21200000, BP_DIG },
    { 21275000, 21450000, BP_ALL },
    { 24890000, 24915000, BP_CW  }, { 24915000, 24929000, BP_DIG }, { 24929000, 24931000, BP_BCN },
    { 24931000, 24990000, BP_ALL },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28200000, BP_DIG }, { 28200000, 28300000, BP_BCN },
    { 28300000, 29700000, BP_ALL },
    { 50000000, 50060000, BP_CW  }, { 50060000, 50080000, BP_BCN }, { 50080000, 50100000, BP_CW  },
    { 50100000, 50300000, BP_ALL }, { 50300000, 50350000, BP_DIG }, { 50350000, 54000000, BP_ALL },
};

/* Technician. HF is 97.301(e), shared with Novice: CW on 80, 40 and 15 m, and 10 m
 * to 28.5 (data to 28.3, phone 28.3-28.5, 97.305). 6 m and the rest of VHF are
 * 97.301(a), which includes Technician and does not include Novice. No 160, 60, 40 m
 * phone, 30, 20, 17 or 12 m. */
static const bp_seg_t plan_usa_tech[] = {
    {  3525000,  3600000, BP_CW  },
    {  7025000,  7125000, BP_CW  },
    { 21025000, 21200000, BP_CW  },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28200000, BP_DIG }, { 28200000, 28300000, BP_BCN },
    { 28300000, 28500000, BP_ALL },
    { 50000000, 50060000, BP_CW  }, { 50060000, 50080000, BP_BCN }, { 50080000, 50100000, BP_CW  },
    { 50100000, 50300000, BP_ALL }, { 50300000, 50350000, BP_DIG }, { 50350000, 54000000, BP_ALL },
};

/* Novice HF is the same 97.301(e) rows as Technician. No 6 m: 97.301(a) starts at
 * Technician. Above HF, 97.301(e) gives Novice 222-225 and 1270-1295 only; those are
 * not drawn here (the strip's tables stop at 6 m) and are handled with the warning. */
static const bp_seg_t plan_usa_nov[] = {
    {  3525000,  3600000, BP_CW  },
    {  7025000,  7125000, BP_CW  },
    { 21025000, 21200000, BP_CW  },
    { 28000000, 28070000, BP_CW  }, { 28070000, 28200000, BP_DIG }, { 28200000, 28300000, BP_BCN },
    { 28300000, 28500000, BP_ALL },
};

/* ------------------------------------------------------------------ prefixes */

/* A call sign series and the ITU region of the place that uses it. Only Region 2 and
 * Region 3 are listed. Anything else, including Europe, Africa, the Middle East and the
 * whole of the former USSR (Asiatic Russia and Mongolia included), falls through to Region 1.
 * Longest match wins, so 3D2 (Fiji) is not 3DA (Eswatini) and VP6 (Pitcairn) is not VP2.
 * K, N, W and AA-AL are not in this table: they are the USA plan, and they are tested first.
 * FCC licenses in Guam, Hawaii and the other K prefixes therefore take the USA plan even
 * where the island sits in Region 3. That is the privilege table, not the map. */
static const struct {
    const char *s;
    uint8_t     region;
} pfx[] = {
    /* Region 2: the Americas, Greenland, the Caribbean. */
    { "C6", 2 }, { "CE", 2 }, { "CD", 2 }, { "CC", 2 }, { "CB", 2 }, { "CA", 2 },
    { "CL", 2 }, { "CM", 2 }, { "CO", 2 }, { "CP", 2 },
    { "CX", 2 }, { "CV", 2 }, { "CW", 2 },
    { "CF", 2 }, { "CG", 2 }, { "CH", 2 }, { "CI", 2 }, { "CJ", 2 }, { "CK", 2 },
    { "CY", 2 }, { "CZ", 2 },
    { "FG", 2 }, { "FJ", 2 }, { "FM", 2 }, { "FP", 2 }, { "FS", 2 }, { "FY", 2 },
    { "HC", 2 }, { "HD", 2 }, { "HH", 2 }, { "HI", 2 }, { "HJ", 2 }, { "HK", 2 },
    { "HO", 2 }, { "HP", 2 }, { "HQ", 2 }, { "HR", 2 }, { "HT", 2 }, { "HU", 2 },
    { "J3", 2 }, { "J6", 2 }, { "J7", 2 }, { "J8", 2 },
    { "LO", 2 }, { "LP", 2 }, { "LQ", 2 }, { "LR", 2 }, { "LS", 2 }, { "LT", 2 },
    { "LU", 2 }, { "LV", 2 }, { "LW", 2 },
    { "L2", 2 }, { "L3", 2 }, { "L4", 2 }, { "L5", 2 }, { "L6", 2 }, { "L7", 2 }, { "L8", 2 }, { "L9", 2 },
    { "OA", 2 }, { "OB", 2 }, { "OC", 2 }, { "OX", 2 },
    { "P4", 2 }, { "PJ", 2 }, { "PP", 2 }, { "PQ", 2 }, { "PR", 2 }, { "PS", 2 },
    { "PT", 2 }, { "PU", 2 }, { "PV", 2 }, { "PW", 2 }, { "PX", 2 }, { "PY", 2 }, { "PZ", 2 },
    { "TE", 2 }, { "TG", 2 }, { "TD", 2 }, { "TI", 2 }, { "T4", 2 },
    { "V2", 2 }, { "V3", 2 }, { "V4", 2 }, { "VA", 2 }, { "VE", 2 }, { "VO", 2 }, { "VX", 2 }, { "VY", 2 },
    { "VP2", 2 }, { "VP5", 2 }, { "VP8", 2 }, { "VP9", 2 },
    { "XA", 2 }, { "XB", 2 }, { "XC", 2 }, { "XD", 2 }, { "XE", 2 }, { "XF", 2 },
    { "XG", 2 }, { "XH", 2 }, { "XI", 2 }, { "XJ", 2 }, { "XK", 2 }, { "XL", 2 },
    { "XM", 2 }, { "XN", 2 }, { "XO", 2 }, { "XQ", 2 }, { "XR", 2 },
    { "YN", 2 }, { "YS", 2 }, { "YV", 2 }, { "YW", 2 }, { "YX", 2 }, { "YY", 2 },
    { "ZF", 2 }, { "ZP", 2 }, { "ZV", 2 }, { "ZW", 2 }, { "ZX", 2 }, { "ZY", 2 }, { "ZZ", 2 },
    { "3E", 2 }, { "3F", 2 }, { "3G", 2 },
    { "4A", 2 }, { "4B", 2 }, { "4C", 2 }, { "4M", 2 }, { "4T", 2 },
    { "5J", 2 }, { "5K", 2 },
    { "6D", 2 }, { "6E", 2 }, { "6F", 2 }, { "6G", 2 }, { "6H", 2 }, { "6I", 2 }, { "6J", 2 }, { "6Y", 2 },
    { "8P", 2 }, { "8R", 2 },
    { "9Y", 2 }, { "9Z", 2 },
    { "AY", 2 }, { "AZ", 2 },
    { "H3", 2 }, { "H6", 2 }, { "H7", 2 }, { "H8", 2 }, { "H9", 2 },

    /* Region 3: most of Asia outside the former USSR, and Oceania. Iran and Afghanistan are
     * Region 3; Turkey and the Arab states are Region 1 and are not listed. */
    { "A3", 3 }, { "A5", 3 }, { "AP", 3 }, { "AT", 3 }, { "AU", 3 }, { "AV", 3 }, { "AW", 3 }, { "AX", 3 },
    { "B", 3 },
    { "C2", 3 },
    { "DS", 3 }, { "DT", 3 }, { "DU", 3 }, { "DV", 3 }, { "DW", 3 }, { "DX", 3 }, { "DY", 3 },
    { "D7", 3 }, { "D8", 3 }, { "D9", 3 },
    { "E2", 3 }, { "E5", 3 }, { "E6", 3 }, { "EP", 3 }, { "EQ", 3 },
    { "FK", 3 }, { "FO", 3 }, { "FT5", 3 }, { "FW", 3 },
    { "H4", 3 }, { "HL", 3 }, { "HS", 3 },
    { "JA", 3 }, { "JB", 3 }, { "JC", 3 }, { "JD", 3 }, { "JE", 3 }, { "JF", 3 }, { "JG", 3 },
    { "JH", 3 }, { "JI", 3 }, { "JJ", 3 }, { "JK", 3 }, { "JL", 3 }, { "JM", 3 }, { "JN", 3 },
    { "JO", 3 }, { "JP", 3 }, { "JQ", 3 }, { "JR", 3 }, { "JS", 3 },
    { "P2", 3 }, { "P5", 3 },
    { "S2", 3 }, { "S3", 3 },
    { "T2", 3 }, { "T3", 3 }, { "T6", 3 }, { "T8", 3 },
    { "V6", 3 }, { "V7", 3 }, { "V8", 3 },
    { "VH", 3 }, { "VI", 3 }, { "VJ", 3 }, { "VK", 3 }, { "VL", 3 }, { "VM", 3 }, { "VN", 3 },
    { "VR", 3 }, { "VU", 3 }, { "VZ", 3 },
    { "VP6", 3 }, { "VQ9", 3 },
    { "XU", 3 }, { "XW", 3 }, { "XV", 3 }, { "XX9", 3 }, { "XY", 3 }, { "XZ", 3 },
    { "YA", 3 }, { "YB", 3 }, { "YC", 3 }, { "YD", 3 }, { "YE", 3 }, { "YF", 3 }, { "YG", 3 }, { "YH", 3 },
    { "YJ", 3 },
    { "ZL", 3 }, { "ZM", 3 },
    { "1S", 3 },
    { "3D2", 3 }, { "3W", 3 },
    { "4D", 3 }, { "4E", 3 }, { "4F", 3 }, { "4G", 3 }, { "4H", 3 }, { "4I", 3 }, { "4S", 3 },
    { "5W", 3 },
    { "6K", 3 }, { "6L", 3 }, { "6M", 3 }, { "6N", 3 },
    { "6P", 3 }, { "6Q", 3 }, { "6R", 3 }, { "6S", 3 },
    { "7A", 3 }, { "7B", 3 }, { "7C", 3 }, { "7D", 3 }, { "7E", 3 }, { "7F", 3 }, { "7G", 3 }, { "7H", 3 }, { "7I", 3 },
    { "7J", 3 }, { "7K", 3 }, { "7L", 3 }, { "7M", 3 }, { "7N", 3 },
    { "8A", 3 }, { "8B", 3 }, { "8C", 3 }, { "8D", 3 }, { "8E", 3 }, { "8F", 3 }, { "8G", 3 }, { "8H", 3 }, { "8I", 3 },
    { "8J", 3 }, { "8K", 3 }, { "8L", 3 }, { "8M", 3 }, { "8N", 3 },
    { "8Q", 3 }, { "8T", 3 }, { "8U", 3 }, { "8V", 3 }, { "8W", 3 }, { "8X", 3 }, { "8Y", 3 },
    { "9M", 3 }, { "9N", 3 }, { "9V", 3 }, { "9W", 3 },
};

/* A suffix that says how the station is operating, not where. The place is the other part. */
static int portable_suffix(const char *s) {
    static const char *const suf[] = { "P", "M", "MM", "AM", "QRP", "A", "R" };
    size_t i;

    if (s[0] >= '0' && s[0] <= '9' && s[1] == '\0') {
        return 1;
    }
    for (i = 0; i < sizeof(suf) / sizeof(suf[0]); i++) {
        if (strcmp(s, suf[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A home call, as opposed to a prefix standing alone ("DL", "KH6", "EA8"). */
static int full_call(const char *s) {
    size_t n = strlen(s);
    int    digit = 0, letter = 0;
    size_t i;

    if (n < 4 || !isalpha((unsigned char)s[n - 1])) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (isdigit(c)) {
            digit = 1;
        } else if (isalpha(c)) {
            letter = 1;
        } else {
            return 0;
        }
    }
    return digit && letter;
}

/* The place the callsign is operating from, upper case, portable designators removed. */
static void location_token(const char *call, char *out, size_t n) {
    char        buf[40];
    char       *parts[4];
    size_t      k = 0;
    int         np = 0;
    const char *pick;
    const char *p;

    out[0] = '\0';
    if (!call || n == 0) {
        return;
    }
    for (p = call; *p && k + 1 < sizeof(buf); p++) {
        unsigned char c = (unsigned char)*p;

        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/') {
            buf[k++] = (char)c;
        }
    }
    buf[k] = '\0';
    if (k == 0) {
        return;
    }

    parts[np++] = buf;
    for (p = buf; *p && np < 4; p++) {
        if (*p == '/') {
            char *sl = (char *)p;

            *sl = '\0';
            parts[np++] = sl + 1;
            p = sl;
        }
    }
    while (np >= 2 && parts[np - 1][0] == '\0') {
        np--;
    }
    if (np >= 2 && portable_suffix(parts[np - 1])) {
        np--;
    }
    pick = parts[0];
    if (np >= 2) {
        /* "DL/W1AW" keeps the prefix. "W1AW/KH6" and "W1AW/VE3ABC" keep the right-hand place. */
        pick = full_call(parts[0]) ? parts[1] : parts[0];
    }
    snprintf(out, n, "%s", pick);
}

/* K, N, W and AA-AL are the United States series (Appendix 42). AA-AL is the block, so AH
 * (Hawaii) is included and AM (Spain) is not. */
static int usa_series(const char *t) {
    if (t[0] == 'K' || t[0] == 'N' || t[0] == 'W') {
        return 1;
    }
    if (t[0] == 'A' && t[1] >= 'A' && t[1] <= 'L') {
        return 1;
    }
    return 0;
}

static int region_of(const char *token) {
    int    best = 0;
    int    region = 0;
    size_t i;

    for (i = 0; i < sizeof(pfx) / sizeof(pfx[0]); i++) {
        size_t n = strlen(pfx[i].s);

        if ((int)n > best && strncmp(token, pfx[i].s, n) == 0) {
            best = (int)n;
            region = pfx[i].region;
        }
    }
    return region;
}

int band_plan_from_call(const char *call) {
    char token[16];
    int  region;

    location_token(call, token, sizeof(token));
    if (token[0] == '\0') {
        return BP_PLAN_R1;
    }
    if (usa_series(token)) {
        return BP_PLAN_USA;
    }
    region = region_of(token);
    if (region == 2) {
        return BP_PLAN_R2;
    }
    if (region == 3) {
        return BP_PLAN_R3;
    }
    return BP_PLAN_R1;
}

int band_plan_resolve(int sel, const char *call) {
    if (sel == BP_PLAN_R1 || sel == BP_PLAN_R2 || sel == BP_PLAN_R3 || sel == BP_PLAN_USA) {
        return sel;
    }
    if (sel != BP_PLAN_AUTO) {
        return BP_PLAN_R1;
    }
    return band_plan_from_call(call);
}

static int class_ok(int cls) {
    if (cls < BP_CLASS_EXTRA || cls > BP_CLASS_NOVICE) {
        return BP_CLASS_EXTRA;
    }
    return cls;
}

int band_plan_table(int plan, int cls, const bp_seg_t **out) {
    const bp_seg_t *t = NULL;
    int             n = 0;

    switch (plan) {
    case BP_PLAN_R1: t = plan_r1; n = (int)(sizeof(plan_r1) / sizeof(plan_r1[0])); break;
    case BP_PLAN_R2: t = plan_r2; n = (int)(sizeof(plan_r2) / sizeof(plan_r2[0])); break;
    case BP_PLAN_R3: t = plan_r3; n = (int)(sizeof(plan_r3) / sizeof(plan_r3[0])); break;
    case BP_PLAN_USA:
        switch (class_ok(cls)) {
        case BP_CLASS_ADVANCED:   t = plan_usa_adv;  n = (int)(sizeof(plan_usa_adv) / sizeof(plan_usa_adv[0])); break;
        case BP_CLASS_GENERAL:    t = plan_usa_gen;  n = (int)(sizeof(plan_usa_gen) / sizeof(plan_usa_gen[0])); break;
        case BP_CLASS_TECHNICIAN: t = plan_usa_tech; n = (int)(sizeof(plan_usa_tech) / sizeof(plan_usa_tech[0])); break;
        case BP_CLASS_NOVICE:     t = plan_usa_nov;  n = (int)(sizeof(plan_usa_nov) / sizeof(plan_usa_nov[0])); break;
        default:                  t = plan_usa;      n = (int)(sizeof(plan_usa) / sizeof(plan_usa[0])); break;
        }
        break;
    default:
        return 0;
    }
    if (out) {
        *out = t;
    }
    return n;
}

/* Above 6 m, where the strip's tables stop. least is the least privileged class that may
 * transmit there (Extra is 0, so a smaller class number is included). Novice is only
 * 222-225 and 1270-1295, from 97.301(e). Technician and above have 97.301(a). */
static const struct {
    int32_t a, e;
    int     least;
} usa_vhf[] = {
    {  144000000,  148000000, BP_CLASS_TECHNICIAN },
    {  219000000,  220000000, BP_CLASS_TECHNICIAN },
    {  222000000,  225000000, BP_CLASS_NOVICE },
    {  420000000,  450000000, BP_CLASS_TECHNICIAN },
    {  902000000,  928000000, BP_CLASS_TECHNICIAN },
    { 1240000000, 1300000000, BP_CLASS_TECHNICIAN },
    { 1270000000, 1295000000, BP_CLASS_NOVICE },
};

static int in_segs(const bp_seg_t *t, int n, int32_t hz) {
    int i;

    for (i = 0; i < n; i++) {
        if (hz >= t[i].start && hz < t[i].end) {
            return 1;
        }
    }
    return 0;
}

static int vhf_hit(int32_t hz, int cls, int any) {
    size_t i;

    for (i = 0; i < sizeof(usa_vhf) / sizeof(usa_vhf[0]); i++) {
        if (hz < usa_vhf[i].a || hz >= usa_vhf[i].e) {
            continue;
        }
        if (any || cls <= usa_vhf[i].least) {
            return 1;
        }
    }
    return 0;
}

int band_plan_tx_warn(int plan, int cls, int32_t freq) {
    int c;
    int known = 0;

    if (plan != BP_PLAN_USA) {
        return 0;
    }
    cls = class_ok(cls);
    {
        const bp_seg_t *mine = NULL;
        int             n    = band_plan_table(BP_PLAN_USA, cls, &mine);

        if (in_segs(mine, n, freq) || vhf_hit(freq, cls, 0)) {
            return 0;
        }
    }
    for (c = BP_CLASS_EXTRA; c <= BP_CLASS_NOVICE; c++) {
        const bp_seg_t *t = NULL;
        int             n = band_plan_table(BP_PLAN_USA, c, &t);

        if (in_segs(t, n, freq)) {
            known = 1;
            break;
        }
    }
    if (!known) {
        known = vhf_hit(freq, 0, 1);
    }
    return known;
}

static int edge_cmp(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;

    return x < y ? -1 : x > y;
}

int band_plan_keep_span(int plan, int cls, int32_t start, int32_t end,
                        int32_t *out_s, int32_t *out_e, int max) {
    int32_t edge[512];
    int     ne = 0;
    int     nout = 0;
    int     c, i;

    if (!out_s || !out_e || max <= 0 || start >= end) {
        return 0;
    }
    if (plan != BP_PLAN_USA) {
        out_s[0] = start;
        out_e[0] = end;
        return 1;
    }
    cls = class_ok(cls);
    edge[ne++] = start;
    edge[ne++] = end;
    for (c = BP_CLASS_EXTRA; c <= BP_CLASS_NOVICE; c++) {
        const bp_seg_t *t = NULL;
        int             n = band_plan_table(BP_PLAN_USA, c, &t);

        for (i = 0; i < n; i++) {
            if (t[i].start > start && t[i].start < end && ne < (int)(sizeof(edge) / sizeof(edge[0]))) {
                edge[ne++] = t[i].start;
            }
            if (t[i].end > start && t[i].end < end && ne < (int)(sizeof(edge) / sizeof(edge[0]))) {
                edge[ne++] = t[i].end;
            }
        }
    }
    for (i = 0; i < (int)(sizeof(usa_vhf) / sizeof(usa_vhf[0])); i++) {
        if (usa_vhf[i].a > start && usa_vhf[i].a < end && ne < (int)(sizeof(edge) / sizeof(edge[0]))) {
            edge[ne++] = usa_vhf[i].a;
        }
        if (usa_vhf[i].e > start && usa_vhf[i].e < end && ne < (int)(sizeof(edge) / sizeof(edge[0]))) {
            edge[ne++] = usa_vhf[i].e;
        }
    }
    qsort(edge, (size_t)ne, sizeof(edge[0]), edge_cmp);
    for (i = 0; i < ne - 1 && nout < max; i++) {
        int32_t a = edge[i], b = edge[i + 1];
        int32_t mid;

        if (a >= b) {
            continue;
        }
        mid = a + (b - a) / 2;
        if (band_plan_tx_warn(BP_PLAN_USA, cls, mid)) {
            continue;
        }
        if (nout > 0 && out_e[nout - 1] == a) {
            out_e[nout - 1] = b;
        } else {
            out_s[nout] = a;
            out_e[nout] = b;
            nout++;
        }
    }
    return nout;
}
