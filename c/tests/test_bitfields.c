/**
 * @file
 * @brief L0 vectors for the internal MD bitfield / wire-format machinery.
 *
 * Exercises MASK()/EXTRACT()/INSERT() (bitfields.h) directly against the
 * field layouts declared in md.h: the shared 4-bit "header" nibble at bits
 * 44-47, and the six pixel-layout variants that share coord_x @ 28 (8 bits)
 * and coord_y @ 36 (8 bits), differing only in how they pack their
 * mode-specific low 28 bits (0-27).
 *
 * @author Petr Mánek
 * @date 20.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <katherine/acquisition.h>

#include "bitfields.h"
#include "protocol/crd.h"
#include "protocol/md.h"
#include "ktest.h"

#define MD_SIZE 6

/* Demo field values shared across the per-layout vectors below: a value per
   field width that is nonzero, distinguishable from the others, and stays
   within its field's mask so the "everything differs" vector below is
   unambiguous. The _14/_10/_4 names refer to field width and stand in for
   whichever field occupies that bit slot in a given layout (toa/integral_tot,
   tot/event_count, ftoa/hit_count respectively). */
#define DEMO_X  0xABu
#define DEMO_Y  0xCDu
#define DEMO_14 0x1234u
#define DEMO_10 0x2AAu
#define DEMO_4  0x5u

/* Builds a 48-bit MD word the way it actually arrives over the wire: pack
   the header nibble and a 44-bit payload into 6 little-endian bytes, then
   reassemble a uint64_t from those bytes, mirroring make_md() in
   test_issue16.c and the byte layout the real read loop decodes. */
static uint64_t
make_md_word(uint8_t header, uint64_t payload)
{
    uint64_t w = ((uint64_t) (header & 0xF) << 44) | (payload & MASK(44));

    unsigned char bytes[MD_SIZE];
    for (int i = 0; i < MD_SIZE; ++i) {
        bytes[i] = (unsigned char) (w >> (8 * i));
    }

    uint64_t rebuilt = 0;
    for (int i = 0; i < MD_SIZE; ++i) {
        rebuilt |= ((uint64_t) bytes[i]) << (8 * i);
    }
    return rebuilt;
}

/* ------------------------------------------------------------------ */
/* MASK()                                                              */

static void
test_mask(void)
{
    KT_CHECK_EQ(MASK(0), 0x0ULL);
    KT_CHECK_EQ(MASK(1), 0x1ULL);
    KT_CHECK_EQ(MASK(4), 0xFULL);
    KT_CHECK_EQ(MASK(8), 0xFFULL);
    KT_CHECK_EQ(MASK(14), 0x3FFFULL);
    KT_CHECK_EQ(MASK(44), 0xFFFFFFFFFFFULL);
}

/* ------------------------------------------------------------------ */
/* md.header (bits 44-47, shared by every MD word regardless of type)  */

static void
test_header_field(void)
{
    KT_CHECK_EQ(EXTRACT(make_md_word(0x0, 0), md, header), 0x0);
    KT_CHECK_EQ(EXTRACT(make_md_word(0x4, 0), md, header), 0x4);
    KT_CHECK_EQ(EXTRACT(make_md_word(0x7, 0), md, header), 0x7);
    KT_CHECK_EQ(EXTRACT(make_md_word(0xC, 0), md, header), 0xC);

    /* An all-ones payload must not bleed upward into a zero header... */
    KT_CHECK_EQ(EXTRACT(make_md_word(0x0, MASK(44)), md, header), 0x0);
    /* ...and an all-ones header must not read out as more than one nibble. */
    KT_CHECK_EQ(EXTRACT(make_md_word(0xF, MASK(44)), md, header), 0xF);
}

/* ------------------------------------------------------------------ */
/* pmd_f_toa_tot: ftoa @ 0/4, tot @ 4/10, toa @ 14/14, coord_x @ 28/8,  */
/* coord_y @ 36/8                                                      */

static void
test_pmd_f_toa_tot_extract(void)
{
    uint64_t payload = ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14)
        | ((uint64_t) DEMO_10 << 4) | (uint64_t) DEMO_4;
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, toa), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, tot), DEMO_10);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, ftoa), DEMO_4);

    /* Every field at its maximum: catches masks that are too narrow. */
    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | (MASK(10) << 4) | MASK(4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_tot, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_tot, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_tot, toa), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_tot, tot), MASK(10));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_tot, ftoa), MASK(4));
}

/* ------------------------------------------------------------------ */
/* pmd_toa_tot: hit_count @ 0/4, tot @ 4/10, toa @ 14/14, coord_x @     */
/* 28/8, coord_y @ 36/8 -- identical layout to pmd_f_toa_tot, renamed   */
/* low field                                                           */

static void
test_pmd_toa_tot_extract(void)
{
    uint64_t payload = ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14)
        | ((uint64_t) DEMO_10 << 4) | (uint64_t) DEMO_4;
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, toa), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, tot), DEMO_10);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, hit_count), DEMO_4);

    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | (MASK(10) << 4) | MASK(4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_tot, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_tot, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_tot, toa), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_tot, tot), MASK(10));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_tot, hit_count), MASK(4));
}

/* ------------------------------------------------------------------ */
/* pmd_f_toa_only: ftoa @ 0/4, toa @ 14/14, coord_x @ 28/8, coord_y @   */
/* 36/8 -- bits 4-13 are unused in this layout                         */

static void
test_pmd_f_toa_only_extract(void)
{
    uint64_t payload =
        ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14) | (uint64_t) DEMO_4;
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_only, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_only, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_only, toa), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_only, ftoa), DEMO_4);

    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | MASK(4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_only, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_only, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_only, toa), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_toa_only, ftoa), MASK(4));
}

/* ------------------------------------------------------------------ */
/* pmd_toa_only: hit_count @ 0/4, toa @ 14/14, coord_x @ 28/8, coord_y  */
/* @ 36/8 -- identical layout to pmd_f_toa_only, renamed low field      */

static void
test_pmd_toa_only_extract(void)
{
    uint64_t payload =
        ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14) | (uint64_t) DEMO_4;
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_toa_only, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_only, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_only, toa), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_only, hit_count), DEMO_4);

    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | MASK(4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_only, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_only, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_only, toa), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_toa_only, hit_count), MASK(4));
}

/* ------------------------------------------------------------------ */
/* pmd_f_event_itot: event_count @ 4/10, integral_tot @ 14/14, coord_x  */
/* @ 28/8, coord_y @ 36/8. Bits 0-3 are dummy with the fast oscillator  */
/* on (Figure 1, p8), so this layout has no field for them -- the one    */
/* mode whose fast variant carries less than its slow one.               */

static void
test_pmd_f_event_itot_extract(void)
{
    /* Bits [3:0] deliberately left clear: this layout has no field there. */
    uint64_t payload =
        ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14) | ((uint64_t) DEMO_10 << 4);
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_f_event_itot, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_event_itot, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_event_itot, integral_tot), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_event_itot, event_count), DEMO_10);

    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | (MASK(10) << 4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_event_itot, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_event_itot, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_event_itot, integral_tot), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_f_event_itot, event_count), MASK(10));
}

/* ------------------------------------------------------------------ */
/* pmd_event_itot: hit_count @ 0/4, event_count @ 4/10, integral_tot @  */
/* 14/14, coord_x @ 28/8, coord_y @ 36/8. With the fast oscillator off,  */
/* bits 0-3 carry the pixel hit counter, measured on hardware as a true  */
/* count saturating at 14.                                              */

static void
test_pmd_event_itot_extract(void)
{
    uint64_t payload = ((uint64_t) DEMO_Y << 36) | ((uint64_t) DEMO_X << 28) | ((uint64_t) DEMO_14 << 14)
        | ((uint64_t) DEMO_10 << 4) | (uint64_t) DEMO_4;
    uint64_t w = make_md_word(0x4, payload);

    KT_CHECK_EQ(EXTRACT(w, pmd_event_itot, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_event_itot, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_event_itot, integral_tot), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_event_itot, event_count), DEMO_10);
    KT_CHECK_EQ(EXTRACT(w, pmd_event_itot, hit_count), DEMO_4);

    uint64_t payload_max = (MASK(8) << 36) | (MASK(8) << 28) | (MASK(14) << 14) | (MASK(10) << 4) | MASK(4);
    uint64_t w_max       = make_md_word(0x4, payload_max);

    KT_CHECK_EQ(EXTRACT(w_max, pmd_event_itot, coord_x), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_event_itot, coord_y), MASK(8));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_event_itot, integral_tot), MASK(14));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_event_itot, event_count), MASK(10));
    KT_CHECK_EQ(EXTRACT(w_max, pmd_event_itot, hit_count), MASK(4));
}

/* ------------------------------------------------------------------ */
/* INSERT()/EXTRACT() round-trip and non-disturbance                   */

static void
test_insert_extract_roundtrip(void)
{
    uint64_t w;

    /* md.header */
    w = INSERT(0ULL, md, header, 0x4);
    KT_CHECK_EQ(w, (uint64_t) 0x4 << 44);
    KT_CHECK_EQ(EXTRACT(w, md, header), 0x4);

    /* pmd_f_toa_tot: insert each field into a zero word in turn and check
       the whole word -- not just the field itself -- to prove neighbouring
       fields (and any reserved bits) are left at zero. */
    w = INSERT(0ULL, pmd_f_toa_tot, ftoa, DEMO_4);
    KT_CHECK_EQ(w, (uint64_t) DEMO_4);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, ftoa), DEMO_4);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, tot), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, toa), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_x), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_y), 0);

    w = INSERT(0ULL, pmd_f_toa_tot, tot, DEMO_10);
    KT_CHECK_EQ(w, (uint64_t) DEMO_10 << 4);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, tot), DEMO_10);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, ftoa), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, toa), 0);

    w = INSERT(0ULL, pmd_f_toa_tot, toa, DEMO_14);
    KT_CHECK_EQ(w, (uint64_t) DEMO_14 << 14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, toa), DEMO_14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, tot), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_x), 0);

    w = INSERT(0ULL, pmd_f_toa_tot, coord_x, DEMO_X);
    KT_CHECK_EQ(w, (uint64_t) DEMO_X << 28);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_x), DEMO_X);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, toa), 0);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_y), 0);

    w = INSERT(0ULL, pmd_f_toa_tot, coord_y, DEMO_Y);
    KT_CHECK_EQ(w, (uint64_t) DEMO_Y << 36);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_y), DEMO_Y);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_tot, coord_x), 0);

    /* The remaining layouts share bit slots with pmd_f_toa_tot above (just
       under different field names, or with a gap where a field is absent);
       a whole-word comparison after inserting into a zero word is enough
       to confirm both placement and non-disturbance for each of them. */
    w = INSERT(0ULL, pmd_toa_tot, hit_count, DEMO_4);
    KT_CHECK_EQ(w, (uint64_t) DEMO_4);
    KT_CHECK_EQ(EXTRACT(w, pmd_toa_tot, hit_count), DEMO_4);

    w = INSERT(0ULL, pmd_f_toa_only, ftoa, DEMO_4);
    KT_CHECK_EQ(w, (uint64_t) DEMO_4);
    w = INSERT(0ULL, pmd_f_toa_only, toa, DEMO_14);
    KT_CHECK_EQ(w, (uint64_t) DEMO_14 << 14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_toa_only, ftoa), 0);

    w = INSERT(0ULL, pmd_toa_only, hit_count, DEMO_4);
    KT_CHECK_EQ(w, (uint64_t) DEMO_4);
    w = INSERT(0ULL, pmd_toa_only, toa, DEMO_14);
    KT_CHECK_EQ(w, (uint64_t) DEMO_14 << 14);

    w = INSERT(0ULL, pmd_f_event_itot, event_count, DEMO_10);
    KT_CHECK_EQ(w, (uint64_t) DEMO_10 << 4);
    w = INSERT(0ULL, pmd_f_event_itot, integral_tot, DEMO_14);
    KT_CHECK_EQ(w, (uint64_t) DEMO_14 << 14);
    KT_CHECK_EQ(EXTRACT(w, pmd_f_event_itot, event_count), 0);

    w = INSERT(0ULL, pmd_event_itot, hit_count, DEMO_4);
    KT_CHECK_EQ(w, (uint64_t) DEMO_4);
    w = INSERT(0ULL, pmd_event_itot, event_count, DEMO_10);
    KT_CHECK_EQ(w, (uint64_t) DEMO_10 << 4);
    w = INSERT(0ULL, pmd_event_itot, integral_tot, DEMO_14);
    KT_CHECK_EQ(w, (uint64_t) DEMO_14 << 14);
    w = INSERT(0ULL, pmd_event_itot, coord_x, DEMO_X);
    KT_CHECK_EQ(w, (uint64_t) DEMO_X << 28);
    w = INSERT(0ULL, pmd_event_itot, coord_y, DEMO_Y);
    KT_CHECK_EQ(w, (uint64_t) DEMO_Y << 36);
}

/* A communication-status response captured from a Gen1 readout with a
   Timepix3 attached, byte for byte. The device's output-block register read
   0x0981 in the same session: channel mask 0x81, so two links, at 320 MHz
   dual-edge, so 640 Mb/s each -- 1280 Mb/s aggregate, which is eight times
   the 160 this field carries. */
static void
test_comm_status_crd_extract(void)
{
    static const uint8_t reply[8] = {0x81, 0xa0, 0x01, 0x00, 0x00, 0x00, 0x18, 0x00};

    uint64_t w = 0;
    for (int i = 7; i >= 0; --i) w = (w << 8) | reply[i];

    KT_CHECK_EQ(EXTRACT(w, comm_status_crd, comm_lines_mask), 0x81u);
    KT_CHECK_EQ(EXTRACT(w, comm_status_crd, total_data_rate), 160u);
    KT_CHECK_EQ(EXTRACT(w, comm_status_crd, chip_detected_flag), 1u);
}

int
main(void)
{
    KT_RUN(test_mask);
    KT_RUN(test_header_field);
    KT_RUN(test_pmd_f_toa_tot_extract);
    KT_RUN(test_pmd_toa_tot_extract);
    KT_RUN(test_pmd_f_toa_only_extract);
    KT_RUN(test_pmd_toa_only_extract);
    KT_RUN(test_pmd_f_event_itot_extract);
    KT_RUN(test_pmd_event_itot_extract);
    KT_RUN(test_insert_extract_roundtrip);
    KT_RUN(test_comm_status_crd_extract);
    return kt_summary();
}
