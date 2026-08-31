/**
 * @file
 * @brief Internal bitfield definitions for measurement data.
 * @author Petr Mánek
 * @date 28.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "bitfields.h"
#include <katherine/px.h>

/* MD stands for Measurement Data, the 6 byte
 * messages sent during (or after) acquisition.
 *
 * There are various types of MD's, some contain
 * metadata about the measurement, others can be
 * directly mapped to active pixels. Below are
 * defined all MD's recognized by this library.
 */

#define _BITS_md_header_start                44
#define _BITS_md_header_mask                 MASK(4)
#define _BITS_md_header_type                 uint8_t

#define _BITS_md_time_offset_offset_start    0
#define _BITS_md_time_offset_offset_mask     MASK(32)
#define _BITS_md_time_offset_offset_type     uint32_t

#define _BITS_md_new_frame_offset_start      32
#define _BITS_md_new_frame_offset_mask       MASK(12)
#define _BITS_md_new_frame_offset_type       uint16_t

#define _BITS_md_frame_finished_n_sent_start 0
#define _BITS_md_frame_finished_n_sent_mask  MASK(44)
#define _BITS_md_frame_finished_n_sent_type  uint64_t

#define _BITS_md_time_lsb_lsb_start          0
#define _BITS_md_time_lsb_lsb_mask           MASK(32)
#define _BITS_md_time_lsb_lsb_type           uint32_t

#define _BITS_md_time_msb_msb_start          0
#define _BITS_md_time_msb_msb_mask           MASK(16)
#define _BITS_md_time_msb_msb_type           uint16_t

#define _BITS_md_lost_px_n_lost_start        0
#define _BITS_md_lost_px_n_lost_mask         MASK(44)
#define _BITS_md_lost_px_n_lost_type         uint64_t


/* For MD's which correspond to pixels, we
 * define a direct mapping function named by
 * the following template:
 *
 *   pmd_{A}_map(dst, src, acq)
 *
 * This function is responsible for mapping MD
 * `src` of bitfield pmd_{A} to a pixel `dst` of
 * type katherine_px_{A}_t. Below are defined
 * such functions for all pixel types.
 */

#define DEFINE_PMD_MAP(SUFFIX) \
    static inline void \
    pmd_##SUFFIX##_map(katherine_px_##SUFFIX##_t *dst, const uint64_t *src, const katherine_acquisition_t *acq)

/* Timestamp-bearing decoders are instantiated once per pixel-clock divider, so
   the coarse-to-fine scale is a constant the compiler can see. Reading it from
   the acquisition instead costs a variable shift, and reading a ratio rather
   than a shift costs a multiply; both measured, both material at the rates this
   loop is built for. The four dividers are the only ones the chip offers, so
   the set is closed. */
#define DEFINE_PMD_MAP_S(SUFFIX, SHIFT) \
    static inline void \
    pmd_##SUFFIX##_s##SHIFT##_map( \
        katherine_px_##SUFFIX##_t *dst, const uint64_t *src, const katherine_acquisition_t *acq)

#define DEFINE_PMD_MAP_EVERY_SHIFT(M) \
    M(2) \
    M(3) \
    M(4) \
    M(5)

#define DEFINE_PMD_PAIR(NAME, TYPE, BASE_TYPE) \
    dst->NAME = (TYPE) EXTRACT(*src, BASE_TYPE, NAME)

/* Combine the chip's coarse and fine counters into one timestamp in fine ticks.

   FTOA is the fine term as an expression rather than a field name, because the
   coarse-only wire formats have no ftoa bit triad and a macro that extracted
   one unconditionally would not compile against them; they pass 0 and the
   compiler folds it away.

   The fine counter measures how far the hit preceded its coarse tick, so it is
   subtracted. That cannot underflow: katherine_acquisition_begin() biases the
   epoch by the larger of one coarse tick and the fine field's span, so the
   difference stays representable without a per-hit test. One coarse tick alone
   would not do at the shorter dividers, where a coarse tick is 4 or 8 fine
   ticks against a field spanning 16. See the timestamp
   notes in px.h -- the bias is visible to callers and must not be removed
   here without removing it there.

   acq->last_toa_offset carries that bias and is kept in fine ticks, so no
   scaling happens per hit.

   The double-column phase offset is ADDED. A later clock phase means a later
   latching edge and so a smaller timestamp, and adding the offset back is what
   equalises hits that arrived together -- measured on I8-W00036, and the
   opposite sign to the reference implementation's data parser. The table is
   filled only when the decoder is the one correcting, so this adds an
   unconditional zero otherwise and needs no test for whether correction is in
   effect. DEFINE_PMD_PAIR_COORD must therefore run FIRST: the lookup reads the
   coordinate this line depends on. */
#define DEFINE_PMD_PAIR_TIMESTAMP(BASE_TYPE, SHIFT, FTOA) \
    dst->timestamp = (((uint64_t) EXTRACT(*src, BASE_TYPE, toa) << (SHIFT)) + acq->last_toa_offset) \
        - (uint64_t) (FTOA) + acq->phase_offsets[dst->coord.x]

#define DEFINE_PMD_PAIR_COORD(BASE_TYPE) \
    { \
        dst->coord.x = (uint8_t) EXTRACT(*src, BASE_TYPE, coord_x); \
        dst->coord.y = (uint8_t) EXTRACT(*src, BASE_TYPE, coord_y); \
    }

#define _BITS_pmd_f_toa_tot_ftoa_start    0
#define _BITS_pmd_f_toa_tot_ftoa_mask     MASK(4)
#define _BITS_pmd_f_toa_tot_ftoa_type     uint16_t

#define _BITS_pmd_f_toa_tot_tot_start     4
#define _BITS_pmd_f_toa_tot_tot_mask      MASK(10)
#define _BITS_pmd_f_toa_tot_tot_type      uint16_t

#define _BITS_pmd_f_toa_tot_toa_start     14
#define _BITS_pmd_f_toa_tot_toa_mask      MASK(14)
#define _BITS_pmd_f_toa_tot_toa_type      uint16_t

#define _BITS_pmd_f_toa_tot_coord_x_start 28
#define _BITS_pmd_f_toa_tot_coord_x_mask  MASK(8)
#define _BITS_pmd_f_toa_tot_coord_x_type  uint16_t

#define _BITS_pmd_f_toa_tot_coord_y_start 36
#define _BITS_pmd_f_toa_tot_coord_y_mask  MASK(8)
#define _BITS_pmd_f_toa_tot_coord_y_type  uint16_t

#define DEFINE_PMD_MAP_F_TOA_TOT(SHIFT) \
    DEFINE_PMD_MAP_S(f_toa_tot, SHIFT) \
    { \
        DEFINE_PMD_PAIR_COORD(pmd_f_toa_tot); \
        DEFINE_PMD_PAIR_TIMESTAMP(pmd_f_toa_tot, SHIFT, EXTRACT(*src, pmd_f_toa_tot, ftoa)); \
        DEFINE_PMD_PAIR(tot, uint16_t, pmd_f_toa_tot); \
    }
DEFINE_PMD_MAP_EVERY_SHIFT(DEFINE_PMD_MAP_F_TOA_TOT)

#define _BITS_pmd_toa_tot_hit_count_start 0
#define _BITS_pmd_toa_tot_hit_count_mask  MASK(4)
#define _BITS_pmd_toa_tot_hit_count_type  uint16_t

#define _BITS_pmd_toa_tot_tot_start       4
#define _BITS_pmd_toa_tot_tot_mask        MASK(10)
#define _BITS_pmd_toa_tot_tot_type        uint16_t

#define _BITS_pmd_toa_tot_toa_start       14
#define _BITS_pmd_toa_tot_toa_mask        MASK(14)
#define _BITS_pmd_toa_tot_toa_type        uint16_t

#define _BITS_pmd_toa_tot_coord_x_start   28
#define _BITS_pmd_toa_tot_coord_x_mask    MASK(8)
#define _BITS_pmd_toa_tot_coord_x_type    uint16_t

#define _BITS_pmd_toa_tot_coord_y_start   36
#define _BITS_pmd_toa_tot_coord_y_mask    MASK(8)
#define _BITS_pmd_toa_tot_coord_y_type    uint16_t

#define DEFINE_PMD_MAP_TOA_TOT(SHIFT) \
    DEFINE_PMD_MAP_S(toa_tot, SHIFT) \
    { \
        DEFINE_PMD_PAIR_COORD(pmd_toa_tot); \
        DEFINE_PMD_PAIR_TIMESTAMP(pmd_toa_tot, SHIFT, 0); \
        DEFINE_PMD_PAIR(hit_count, uint8_t, pmd_toa_tot); \
        DEFINE_PMD_PAIR(tot, uint16_t, pmd_toa_tot); \
    }
DEFINE_PMD_MAP_EVERY_SHIFT(DEFINE_PMD_MAP_TOA_TOT)

#define _BITS_pmd_f_toa_only_ftoa_start    0
#define _BITS_pmd_f_toa_only_ftoa_mask     MASK(4)
#define _BITS_pmd_f_toa_only_ftoa_type     uint16_t

#define _BITS_pmd_f_toa_only_toa_start     14
#define _BITS_pmd_f_toa_only_toa_mask      MASK(14)
#define _BITS_pmd_f_toa_only_toa_type      uint16_t

#define _BITS_pmd_f_toa_only_coord_x_start 28
#define _BITS_pmd_f_toa_only_coord_x_mask  MASK(8)
#define _BITS_pmd_f_toa_only_coord_x_type  uint16_t

#define _BITS_pmd_f_toa_only_coord_y_start 36
#define _BITS_pmd_f_toa_only_coord_y_mask  MASK(8)
#define _BITS_pmd_f_toa_only_coord_y_type  uint16_t

#define DEFINE_PMD_MAP_F_TOA_ONLY(SHIFT) \
    DEFINE_PMD_MAP_S(f_toa_only, SHIFT) \
    { \
        DEFINE_PMD_PAIR_COORD(pmd_f_toa_only); \
        DEFINE_PMD_PAIR_TIMESTAMP(pmd_f_toa_only, SHIFT, EXTRACT(*src, pmd_f_toa_only, ftoa)); \
    }
DEFINE_PMD_MAP_EVERY_SHIFT(DEFINE_PMD_MAP_F_TOA_ONLY)

#define _BITS_pmd_toa_only_hit_count_start 0
#define _BITS_pmd_toa_only_hit_count_mask  MASK(4)
#define _BITS_pmd_toa_only_hit_count_type  uint16_t

#define _BITS_pmd_toa_only_toa_start       14
#define _BITS_pmd_toa_only_toa_mask        MASK(14)
#define _BITS_pmd_toa_only_toa_type        uint16_t

#define _BITS_pmd_toa_only_coord_x_start   28
#define _BITS_pmd_toa_only_coord_x_mask    MASK(8)
#define _BITS_pmd_toa_only_coord_x_type    uint16_t

#define _BITS_pmd_toa_only_coord_y_start   36
#define _BITS_pmd_toa_only_coord_y_mask    MASK(8)
#define _BITS_pmd_toa_only_coord_y_type    uint16_t

#define DEFINE_PMD_MAP_TOA_ONLY(SHIFT) \
    DEFINE_PMD_MAP_S(toa_only, SHIFT) \
    { \
        DEFINE_PMD_PAIR_COORD(pmd_toa_only); \
        DEFINE_PMD_PAIR_TIMESTAMP(pmd_toa_only, SHIFT, 0); \
        DEFINE_PMD_PAIR(hit_count, uint8_t, pmd_toa_only); \
    }
DEFINE_PMD_MAP_EVERY_SHIFT(DEFINE_PMD_MAP_TOA_ONLY)

#define _BITS_pmd_f_event_itot_event_count_start  4
#define _BITS_pmd_f_event_itot_event_count_mask   MASK(10)
#define _BITS_pmd_f_event_itot_event_count_type   uint16_t

#define _BITS_pmd_f_event_itot_integral_tot_start 14
#define _BITS_pmd_f_event_itot_integral_tot_mask  MASK(14)
#define _BITS_pmd_f_event_itot_integral_tot_type  uint16_t

#define _BITS_pmd_f_event_itot_coord_x_start      28
#define _BITS_pmd_f_event_itot_coord_x_mask       MASK(8)
#define _BITS_pmd_f_event_itot_coord_x_type       uint16_t

#define _BITS_pmd_f_event_itot_coord_y_start      36
#define _BITS_pmd_f_event_itot_coord_y_mask       MASK(8)
#define _BITS_pmd_f_event_itot_coord_y_type       uint16_t

DEFINE_PMD_MAP(f_event_itot)
{
    (void) acq;

    DEFINE_PMD_PAIR_COORD(pmd_f_event_itot);
    DEFINE_PMD_PAIR(event_count, uint16_t, pmd_f_event_itot);
    DEFINE_PMD_PAIR(integral_tot, uint16_t, pmd_f_event_itot);
}

/* Bits [3:0] carry the pixel hit counter only while the fast oscillator is
   OFF; with it on they are dummy and the fast variant below has no field for
   them (Tpx3 manual Figure 1, p8). Measured on a Gen1 readout: with the
   oscillator off, a patch given N test pulses reads N here for N up to 14 and
   saturates at 14 -- Table 4's limit for this counter, and what distinguishes
   it from the fine-ToA field, which saturates at 15. */
#define _BITS_pmd_event_itot_hit_count_start    0
#define _BITS_pmd_event_itot_hit_count_mask     MASK(4)
#define _BITS_pmd_event_itot_hit_count_type     uint16_t

#define _BITS_pmd_event_itot_event_count_start  4
#define _BITS_pmd_event_itot_event_count_mask   MASK(10)
#define _BITS_pmd_event_itot_event_count_type   uint16_t

#define _BITS_pmd_event_itot_integral_tot_start 14
#define _BITS_pmd_event_itot_integral_tot_mask  MASK(14)
#define _BITS_pmd_event_itot_integral_tot_type  uint16_t

#define _BITS_pmd_event_itot_coord_x_start      28
#define _BITS_pmd_event_itot_coord_x_mask       MASK(8)
#define _BITS_pmd_event_itot_coord_x_type       uint16_t

#define _BITS_pmd_event_itot_coord_y_start      36
#define _BITS_pmd_event_itot_coord_y_mask       MASK(8)
#define _BITS_pmd_event_itot_coord_y_type       uint16_t

DEFINE_PMD_MAP(event_itot)
{
    (void) acq;

    DEFINE_PMD_PAIR_COORD(pmd_event_itot);
    DEFINE_PMD_PAIR(hit_count, uint8_t, pmd_event_itot);
    DEFINE_PMD_PAIR(event_count, uint16_t, pmd_event_itot);
    DEFINE_PMD_PAIR(integral_tot, uint16_t, pmd_event_itot);
}

#undef DEFINE_PMD_MAP
#undef DEFINE_PMD_MAP_S
#undef DEFINE_PMD_PAIR
#undef DEFINE_PMD_PAIR_COORD
#undef DEFINE_PMD_PAIR_TIMESTAMP

#endif
