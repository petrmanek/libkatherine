/**
 * @file
 * @brief Measurement data generator of the protocol emulator.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <katherine/emulator.h>
#include <katherine/error.h>
/* The pixel mapping functions of md.h are written against the acquisition,
 * so its declaration has to precede them. */
#include <katherine/acquisition.h>
#include "acq/md.h"
#include "emu.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* Measurement data are built with the same field declarations the decoder
 * reads them back with, so that the two can never disagree. */
#define EMU_MD_NEW(HDR) INSERT((uint64_t) 0, md, header, (uint64_t) (HDR))

#define EMU_MD_COORD(MD, BITFIELD, HIT) \
    do { \
        (MD) = INSERT((MD), BITFIELD, coord_x, (uint64_t) (HIT)->x); \
        (MD) = INSERT((MD), BITFIELD, coord_y, (uint64_t) (HIT)->y); \
    } while (0)

/* One emulated hit, in the union of the fields of all pixel layouts. Only
 * the fields of the configured layout reach the wire. */
typedef struct emu_hit {
    uint8_t x, y;
    uint16_t toa;
    uint8_t ftoa;
    uint8_t hit_count;
    uint16_t tot;
    uint16_t event_count;
    uint16_t integral_tot;
} emu_hit_t;

static inline bool
has_room(const katherine_emu_stream_t *stream)
{
    return stream->buf_len + KATHERINE_EMU_MD_SIZE <= sizeof(stream->buf);
}

static void
emit(katherine_emu_stream_t *stream, uint64_t md)
{
    if (!has_room(stream)) return;

    katherine_emu_store_le(stream->buf + stream->buf_len, md, KATHERINE_EMU_MD_SIZE);
    stream->buf_len += KATHERINE_EMU_MD_SIZE;
}

/* Hits are spread evenly over the shutter, so that the consumer of the
 * stream sees them arrive during the frame rather than all at its end. */
static uint64_t
hit_due_ns(const katherine_emu_stream_t *stream, uint32_t index)
{
    uint64_t spacing = stream->frame_len_ns / ((uint64_t) stream->hits + 1);
    return stream->frame_open_ns + spacing * ((uint64_t) index + 1);
}

static uint64_t
frame_close_ns(const katherine_emu_stream_t *stream)
{
    return stream->frame_open_ns + stream->frame_len_ns;
}

static void
draw_hit(katherine_emu_stream_t *stream, uint64_t due_ns, emu_hit_t *hit)
{
    memset(hit, 0, sizeof(*hit));

    switch (stream->pattern) {
    case KATHERINE_EMU_PATTERN_HOT_COLUMN:
        hit->x = stream->hot_column;
        hit->y = (uint8_t) (stream->px_index % KATHERINE_EMU_MATRIX_SIZE);
        break;

    case KATHERINE_EMU_PATTERN_GRADIENT: {
        /* The larger of two uniform draws is distributed linearly, which
           makes the hit density rise across the matrix. */
        uint8_t a = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);
        uint8_t b = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);
        hit->x    = a > b ? a : b;
        hit->y    = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);
        break;
    }

    case KATHERINE_EMU_PATTERN_UNIFORM:
    default:
        hit->x = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);
        hit->y = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);
        break;
    }

    /* The arrival time within the frame, in readout timer ticks, truncated
       to the width of the coarse time-of-arrival field. */
    hit->toa = (uint16_t) (((due_ns - stream->frame_open_ns) / KATHERINE_EMU_TICK_NS) & MASK(14));

    hit->ftoa         = (uint8_t) katherine_emu_prng_below(&stream->rng, 16);
    hit->hit_count    = 1;
    hit->tot          = (uint16_t) katherine_emu_prng_below(&stream->rng, 1024);
    hit->event_count  = (uint16_t) (1 + katherine_emu_prng_below(&stream->rng, 64));
    hit->integral_tot = (uint16_t) ((hit->event_count * hit->tot) & MASK(14));
}

static uint64_t
build_hit_md(const katherine_emu_stream_t *stream, const emu_hit_t *hit)
{
    uint64_t md = EMU_MD_NEW(KATHERINE_EMU_MD_PIXEL);

    switch (stream->acq_mode) {
    case ACQUISITION_MODE_TOA_TOT:
        if (stream->fast_vco) {
            EMU_MD_COORD(md, pmd_f_toa_tot, hit);
            md = INSERT(md, pmd_f_toa_tot, toa, (uint64_t) hit->toa);
            md = INSERT(md, pmd_f_toa_tot, ftoa, (uint64_t) hit->ftoa);
            md = INSERT(md, pmd_f_toa_tot, tot, (uint64_t) hit->tot);
        } else {
            EMU_MD_COORD(md, pmd_toa_tot, hit);
            md = INSERT(md, pmd_toa_tot, toa, (uint64_t) hit->toa);
            md = INSERT(md, pmd_toa_tot, hit_count, (uint64_t) hit->hit_count);
            md = INSERT(md, pmd_toa_tot, tot, (uint64_t) hit->tot);
        }
        break;

    case ACQUISITION_MODE_ONLY_TOA:
        if (stream->fast_vco) {
            EMU_MD_COORD(md, pmd_f_toa_only, hit);
            md = INSERT(md, pmd_f_toa_only, toa, (uint64_t) hit->toa);
            md = INSERT(md, pmd_f_toa_only, ftoa, (uint64_t) hit->ftoa);
        } else {
            EMU_MD_COORD(md, pmd_toa_only, hit);
            md = INSERT(md, pmd_toa_only, toa, (uint64_t) hit->toa);
            md = INSERT(md, pmd_toa_only, hit_count, (uint64_t) hit->hit_count);
        }
        break;

    case ACQUISITION_MODE_EVENT_ITOT:
        if (stream->fast_vco) {
            EMU_MD_COORD(md, pmd_f_event_itot, hit);
            md = INSERT(md, pmd_f_event_itot, hit_count, (uint64_t) hit->hit_count);
            md = INSERT(md, pmd_f_event_itot, event_count, (uint64_t) hit->event_count);
            md = INSERT(md, pmd_f_event_itot, integral_tot, (uint64_t) hit->integral_tot);
        } else {
            EMU_MD_COORD(md, pmd_event_itot, hit);
            md = INSERT(md, pmd_event_itot, event_count, (uint64_t) hit->event_count);
            md = INSERT(md, pmd_event_itot, integral_tot, (uint64_t) hit->integral_tot);
        }
        break;

    default:
        /* An unconfigured mode leaves the pixel fields empty; the header
           still identifies the datum as a pixel. */
        break;
    }

    return md;
}

/* Generate every measurement datum whose time has come, up to the room
 * left in the staging buffer. The position within the acquisition is the
 * frame index, the stage and the hit counter, so an interrupted run simply
 * resumes on the next call. */
static void
pump(katherine_emu_t *emu)
{
    katherine_emu_stream_t *stream = &emu->stream;

    if (stream->buf_pos == stream->buf_len) {
        stream->buf_pos = 0;
        stream->buf_len = 0;
    }

    while (stream->armed && has_room(stream)) {
        uint64_t md;
        uint64_t ticks;

        switch (stream->stage) {
        case KATHERINE_EMU_STAGE_NEW_FRAME:
            if (stream->frame_open_ns > emu->now_ns) return;

            stream->frame_active = true;
            stream->px_index     = 0;
            stream->offset_sent  = false;

            md = EMU_MD_NEW(KATHERINE_EMU_MD_NEW_FRAME);
            md = INSERT(md, md_new_frame, offset, (uint64_t) 0);
            emit(stream, md);
            stream->stage = KATHERINE_EMU_STAGE_START_LSB;
            break;

        case KATHERINE_EMU_STAGE_START_LSB:
            ticks = stream->frame_open_ns / KATHERINE_EMU_TICK_NS;
            md    = EMU_MD_NEW(KATHERINE_EMU_MD_START_TIME_LSB);
            md    = INSERT(md, md_time_lsb, lsb, ticks);
            emit(stream, md);
            stream->stage = KATHERINE_EMU_STAGE_START_MSB;
            break;

        case KATHERINE_EMU_STAGE_START_MSB:
            ticks = (stream->frame_open_ns / KATHERINE_EMU_TICK_NS) >> 32;
            md    = EMU_MD_NEW(KATHERINE_EMU_MD_START_TIME_MSB);
            md    = INSERT(md, md_time_msb, msb, ticks);
            emit(stream, md);
            stream->stage = KATHERINE_EMU_STAGE_PIXELS;
            break;

        case KATHERINE_EMU_STAGE_PIXELS: {
            emu_hit_t hit;
            uint64_t due;

            if (stream->px_index >= stream->hits) {
                stream->stage = KATHERINE_EMU_STAGE_END_LSB;
                break;
            }

            due = hit_due_ns(stream, stream->px_index);
            if (due > emu->now_ns) return;

            /* In the data driven readout the coarse time of arrival wraps
               often, so the readout interleaves the offset of the window
               the following hits belong to. */
            if (stream->readout_mode == READOUT_DATA_DRIVEN && !stream->offset_sent
                && (stream->px_index % KATHERINE_EMU_TOA_OFFSET_PERIOD) == 0) {
                uint64_t offset = ((due - stream->frame_open_ns) / KATHERINE_EMU_TICK_NS) >> 14;
                md              = EMU_MD_NEW(KATHERINE_EMU_MD_TIME_OFFSET);
                md              = INSERT(md, md_time_offset, offset, offset);
                emit(stream, md);
                stream->offset_sent = true;
                break;
            }

            draw_hit(stream, due, &hit);
            emit(stream, build_hit_md(stream, &hit));
            ++stream->px_index;
            stream->offset_sent = false;
            break;
        }

        case KATHERINE_EMU_STAGE_END_LSB:
            if (frame_close_ns(stream) > emu->now_ns) return;

            ticks = frame_close_ns(stream) / KATHERINE_EMU_TICK_NS;
            md    = EMU_MD_NEW(KATHERINE_EMU_MD_END_TIME_LSB);
            md    = INSERT(md, md_time_lsb, lsb, ticks);
            emit(stream, md);
            stream->stage = KATHERINE_EMU_STAGE_END_MSB;
            break;

        case KATHERINE_EMU_STAGE_END_MSB:
            ticks = (frame_close_ns(stream) / KATHERINE_EMU_TICK_NS) >> 32;
            md    = EMU_MD_NEW(KATHERINE_EMU_MD_END_TIME_MSB);
            md    = INSERT(md, md_time_msb, msb, ticks);
            emit(stream, md);
            stream->stage = KATHERINE_EMU_STAGE_LOST;
            break;

        case KATHERINE_EMU_STAGE_LOST:
            if (stream->lost > 0) {
                md = EMU_MD_NEW(KATHERINE_EMU_MD_LOST_PX);
                md = INSERT(md, md_lost_px, n_lost, (uint64_t) stream->lost);
                emit(stream, md);
            }
            stream->stage = KATHERINE_EMU_STAGE_FINISHED;
            break;

        case KATHERINE_EMU_STAGE_FINISHED:
            /* The frame is closed by the count of hits actually sent. */
            md = EMU_MD_NEW(KATHERINE_EMU_MD_FRAME_FINISHED);
            md = INSERT(md, md_frame_finished, n_sent, (uint64_t) stream->px_index);
            emit(stream, md);

            stream->frame_active = false;
            ++stream->frame_index;

            if (stream->frame_index < stream->frames_total) {
                stream->frame_open_ns += stream->frame_len_ns;
                stream->stage = KATHERINE_EMU_STAGE_NEW_FRAME;
            } else {
                stream->armed = false;
                stream->stage = KATHERINE_EMU_STAGE_IDLE;
            }
            break;

        case KATHERINE_EMU_STAGE_IDLE:
        default:
            return;
        }
    }
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Return the measurement data generator to its initial state.
 * @param emu Emulator
 */
KATHERINE_NOT_EXPORTED void
katherine_emu_stream_reset(katherine_emu_t *emu)
{
    memset(&emu->stream, 0, sizeof(emu->stream));
    emu->stream.stage = KATHERINE_EMU_STAGE_IDLE;
}

/**
 * Begin an acquisition, sampling the register file for its parameters.
 * @param emu Emulator
 * @param readout_mode Readout chain selected by the acquisition start command
 */
KATHERINE_NOT_EXPORTED void
katherine_emu_stream_arm(katherine_emu_t *emu, uint8_t readout_mode)
{
    katherine_emu_stream_t *stream = &emu->stream;
    uint64_t units;

    /* Whatever the previous acquisition left staged is gone. */
    stream->buf_len = 0;
    stream->buf_pos = 0;

    stream->readout_mode = readout_mode;
    stream->acq_mode     = emu->regs.acq_mode;
    stream->fast_vco     = emu->regs.fast_vco;

    /* The acquisition time is set as a pair of halves counting ten
       nanosecond units. */
    units                 = ((uint64_t) emu->regs.acq_time_msb << 32) | emu->regs.acq_time_lsb;
    stream->frame_len_ns  = units * 10;
    stream->frames_total  = emu->regs.no_frames;
    stream->frame_index   = 0;
    stream->frame_open_ns = emu->now_ns;
    stream->frame_active  = false;

    stream->pattern     = emu->profile.pattern;
    stream->hits        = emu->profile.hits_per_frame;
    stream->lost        = emu->profile.lost_per_frame;
    stream->px_index    = 0;
    stream->offset_sent = false;

    /* Reseeded per acquisition, so that the data of a run do not depend on
       how many runs preceded it. */
    stream->rng.state  = emu->profile.seed ^ 0x9E3779B97F4A7C15ull;
    stream->hot_column = (uint8_t) katherine_emu_prng_below(&stream->rng, KATHERINE_EMU_MATRIX_SIZE);

    /* A readout asked for no frames measures nothing. */
    stream->armed = stream->frames_total > 0;
    stream->stage = stream->armed ? KATHERINE_EMU_STAGE_NEW_FRAME : KATHERINE_EMU_STAGE_IDLE;
}

/**
 * End an acquisition. A frame still open is terminated by the aborted
 * measurement datum, in place of the frame finished one.
 * @param emu Emulator
 */
KATHERINE_NOT_EXPORTED void
katherine_emu_stream_stop(katherine_emu_t *emu)
{
    katherine_emu_stream_t *stream = &emu->stream;

    if (!stream->armed) return;

    /* Catch up with the virtual clock first: the frame the command
       interrupts is the one open at this instant, whether or not the
       consumer has asked for data recently. */
    pump(emu);
    if (!stream->armed) return;

    if (stream->frame_active) {
        emit(stream, EMU_MD_NEW(KATHERINE_EMU_MD_ABORTED));
        stream->frame_active = false;
    }

    stream->armed = false;
    stream->stage = KATHERINE_EMU_STAGE_IDLE;
}

/**
 * Accrue rate shaping credit for the time that has passed.
 * @param emu Emulator
 * @param ns Amount of time that has passed, in nanoseconds
 */
KATHERINE_NOT_EXPORTED void
katherine_emu_stream_advance(katherine_emu_t *emu, uint64_t ns)
{
    katherine_emu_stream_t *stream = &emu->stream;
    const uint64_t rate            = emu->profile.shape_bytes_per_s;
    uint64_t total;

    if (rate == 0) return;

    if (ns > (UINT64_MAX - 1000000000ull) / rate) {
        /* Absurdly long step: the bucket saturates in any case. */
        stream->tokens     = KATHERINE_EMU_SHAPE_BURST_BYTES;
        stream->token_frac = 0;
        return;
    }

    total = stream->token_frac + ns * rate;
    stream->tokens += total / 1000000000ull;
    stream->token_frac = total % 1000000000ull;

    if (stream->tokens > KATHERINE_EMU_SHAPE_BURST_BYTES) {
        stream->tokens = KATHERINE_EMU_SHAPE_BURST_BYTES;
    }
}

/**
 * Retrieve measurement data produced up to the current virtual time.
 *
 * Only whole measurement data are written, so a buffer shorter than one
 * datum never receives anything.
 *
 * @param emu Emulator
 * @param buf Start of the destination buffer
 * @param cap Capacity of the destination buffer in bytes
 * @param len Number of bytes written (optional)
 * @return Error code, or -KATHERINE_E_TIMEOUT if no data are available yet.
 */
int
katherine_emu_data_out(katherine_emu_t *emu, void *buf, size_t cap, size_t *len)
{
    katherine_emu_stream_t *stream;
    size_t count;

    if (emu == NULL || buf == NULL) return -KATHERINE_E_INVAL;

    stream = &emu->stream;
    pump(emu);

    count = stream->buf_len - stream->buf_pos;
    if (count > cap) count = cap;

    if (emu->profile.shape_bytes_per_s != 0 && count > stream->tokens) {
        count = (size_t) stream->tokens;
    }

    /* Truncate to whole measurement data. */
    count -= count % KATHERINE_EMU_MD_SIZE;
    if (count == 0) return -KATHERINE_E_TIMEOUT;

    memcpy(buf, stream->buf + stream->buf_pos, count);
    stream->buf_pos += count;

    if (emu->profile.shape_bytes_per_s != 0) {
        stream->tokens -= count;
    }

    if (len != NULL) *len = count;
    return 0;
}
