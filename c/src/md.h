/* Katherine Control Library
 *
 * This file was created on 28.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
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

#define _BITS_md_header_start                   44
#define _BITS_md_header_mask                    MASK(4)
#define _BITS_md_header_type                    uint8_t

#define _BITS_md_time_offset_offset_start       0
#define _BITS_md_time_offset_offset_mask        MASK(32)
#define _BITS_md_time_offset_offset_type        uint32_t

#define _BITS_md_new_frame_offset_start         32
#define _BITS_md_new_frame_offset_mask          MASK(12)
#define _BITS_md_new_frame_offset_type          uint16_t

#define _BITS_md_frame_finished_n_sent_start    0
#define _BITS_md_frame_finished_n_sent_mask     MASK(44)
#define _BITS_md_frame_finished_n_sent_type     uint64_t

#define _BITS_md_time_lsb_lsb_start             0
#define _BITS_md_time_lsb_lsb_mask              MASK(32)
#define _BITS_md_time_lsb_lsb_type              uint32_t

#define _BITS_md_time_msb_msb_start             0
#define _BITS_md_time_msb_msb_mask              MASK(16)
#define _BITS_md_time_msb_msb_type              uint16_t

#define _BITS_md_lost_px_n_lost_start           0
#define _BITS_md_lost_px_n_lost_mask            MASK(44)
#define _BITS_md_lost_px_n_lost_type            uint64_t


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
    static inline void\
    pmd_##SUFFIX##_map(katherine_px_##SUFFIX##_t *dst, const uint64_t *src, const katherine_acquisition_t *acq)

#define DEFINE_PMD_PAIR(NAME, TYPE, BASE_TYPE) \
    dst->NAME = (TYPE) EXTRACT(*src, BASE_TYPE, NAME)

#define DEFINE_PMD_PAIR_TOA(BASE_TYPE) \
    dst->toa = (uint64_t) EXTRACT(*src, BASE_TYPE, toa) + acq->last_toa_offset

#define DEFINE_PMD_PAIR_COORD(BASE_TYPE) \
    {\
        dst->coord.x = (uint8_t) EXTRACT(*src, BASE_TYPE, coord_x);\
        dst->coord.y = (uint8_t) EXTRACT(*src, BASE_TYPE, coord_y);\
    }

#define _BITS_pmd_f_toa_tot_ftoa_start      0
#define _BITS_pmd_f_toa_tot_ftoa_mask       MASK(4)
#define _BITS_pmd_f_toa_tot_ftoa_type       uint16_t

#define _BITS_pmd_f_toa_tot_tot_start       4
#define _BITS_pmd_f_toa_tot_tot_mask        MASK(10)
#define _BITS_pmd_f_toa_tot_tot_type        uint16_t

#define _BITS_pmd_f_toa_tot_toa_start       14
#define _BITS_pmd_f_toa_tot_toa_mask        MASK(14)
#define _BITS_pmd_f_toa_tot_toa_type        uint16_t

#define _BITS_pmd_f_toa_tot_coord_x_start   28
#define _BITS_pmd_f_toa_tot_coord_x_mask    MASK(8)
#define _BITS_pmd_f_toa_tot_coord_x_type    uint16_t

#define _BITS_pmd_f_toa_tot_coord_y_start   36
#define _BITS_pmd_f_toa_tot_coord_y_mask    MASK(8)
#define _BITS_pmd_f_toa_tot_coord_y_type    uint16_t

DEFINE_PMD_MAP(f_toa_tot)
{
    DEFINE_PMD_PAIR_COORD(pmd_f_toa_tot);
    DEFINE_PMD_PAIR_TOA(pmd_f_toa_tot);
    DEFINE_PMD_PAIR(ftoa,   uint8_t,    pmd_f_toa_tot);
    DEFINE_PMD_PAIR(tot,    uint16_t,   pmd_f_toa_tot);
}

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

DEFINE_PMD_MAP(toa_tot)
{
    DEFINE_PMD_PAIR_COORD(pmd_toa_tot);
    DEFINE_PMD_PAIR_TOA(pmd_toa_tot);
    DEFINE_PMD_PAIR(hit_count,  uint8_t,    pmd_toa_tot);
    DEFINE_PMD_PAIR(tot,        uint16_t,   pmd_toa_tot);
}

#define _BITS_pmd_f_toa_only_ftoa_start      0
#define _BITS_pmd_f_toa_only_ftoa_mask       MASK(4)
#define _BITS_pmd_f_toa_only_ftoa_type       uint16_t

#define _BITS_pmd_f_toa_only_toa_start       14
#define _BITS_pmd_f_toa_only_toa_mask        MASK(14)
#define _BITS_pmd_f_toa_only_toa_type        uint16_t

#define _BITS_pmd_f_toa_only_coord_x_start   28
#define _BITS_pmd_f_toa_only_coord_x_mask    MASK(8)
#define _BITS_pmd_f_toa_only_coord_x_type    uint16_t

#define _BITS_pmd_f_toa_only_coord_y_start   36
#define _BITS_pmd_f_toa_only_coord_y_mask    MASK(8)
#define _BITS_pmd_f_toa_only_coord_y_type    uint16_t

DEFINE_PMD_MAP(f_toa_only)
{
    DEFINE_PMD_PAIR_COORD(pmd_f_toa_only);
    DEFINE_PMD_PAIR_TOA(pmd_f_toa_only);
    DEFINE_PMD_PAIR(ftoa,   uint8_t,    pmd_f_toa_only);
}

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

DEFINE_PMD_MAP(toa_only)
{
    DEFINE_PMD_PAIR_COORD(pmd_toa_only);
    DEFINE_PMD_PAIR_TOA(pmd_toa_only);
    DEFINE_PMD_PAIR(hit_count,  uint8_t,    pmd_toa_only);
}

#define _BITS_pmd_f_event_itot_hit_count_start      0
#define _BITS_pmd_f_event_itot_hit_count_mask       MASK(4)
#define _BITS_pmd_f_event_itot_hit_count_type       uint16_t

#define _BITS_pmd_f_event_itot_event_count_start    4
#define _BITS_pmd_f_event_itot_event_count_mask     MASK(10)
#define _BITS_pmd_f_event_itot_event_count_type     uint16_t

#define _BITS_pmd_f_event_itot_integral_tot_start   14
#define _BITS_pmd_f_event_itot_integral_tot_mask    MASK(14)
#define _BITS_pmd_f_event_itot_integral_tot_type    uint16_t

#define _BITS_pmd_f_event_itot_coord_x_start        28
#define _BITS_pmd_f_event_itot_coord_x_mask         MASK(8)
#define _BITS_pmd_f_event_itot_coord_x_type         uint16_t

#define _BITS_pmd_f_event_itot_coord_y_start        36
#define _BITS_pmd_f_event_itot_coord_y_mask         MASK(8)
#define _BITS_pmd_f_event_itot_coord_y_type         uint16_t

DEFINE_PMD_MAP(f_event_itot)
{
    DEFINE_PMD_PAIR_COORD(pmd_f_event_itot);
    DEFINE_PMD_PAIR(hit_count,      uint8_t,    pmd_f_event_itot);
    DEFINE_PMD_PAIR(event_count,    uint16_t,   pmd_f_event_itot);
    DEFINE_PMD_PAIR(integral_tot,   uint16_t,   pmd_f_event_itot);
}

#define _BITS_pmd_event_itot_event_count_start 4
#define _BITS_pmd_event_itot_event_count_mask  MASK(10)
#define _BITS_pmd_event_itot_event_count_type  uint16_t

#define _BITS_pmd_event_itot_integral_tot_start 14
#define _BITS_pmd_event_itot_integral_tot_mask  MASK(14)
#define _BITS_pmd_event_itot_integral_tot_type  uint16_t

#define _BITS_pmd_event_itot_coord_x_start   28
#define _BITS_pmd_event_itot_coord_x_mask    MASK(8)
#define _BITS_pmd_event_itot_coord_x_type    uint16_t

#define _BITS_pmd_event_itot_coord_y_start   36
#define _BITS_pmd_event_itot_coord_y_mask    MASK(8)
#define _BITS_pmd_event_itot_coord_y_type    uint16_t

DEFINE_PMD_MAP(event_itot)
{
    DEFINE_PMD_PAIR_COORD(pmd_event_itot);
    DEFINE_PMD_PAIR(event_count,    uint16_t,   pmd_event_itot);
    DEFINE_PMD_PAIR(integral_tot,   uint16_t,   pmd_event_itot);
}

#undef DEFINE_PMD_MAP
#undef DEFINE_PMD_PAIR
#undef DEFINE_PMD_PAIR_COORD
#undef DEFINE_PMD_PAIR_TOA

#endif
