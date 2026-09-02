/**
 * \file
 * \brief Internal helpers of the protocol emulator.
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/emulator.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.
//
// The state of the emulator is declared in katherine/emulator.h, so that
// the caller can provide its storage. What remains here are the constants,
// helpers and cross-file interfaces of the implementation.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// Length of a command datagram: the readout reads a fixed eight bytes.
#define KATHERINE_EMU_CMD_SIZE          8

// The pixel configuration matrix is uploaded as raw words following the
// 0x12 command; the readout consumes exactly this many of them before it
// looks at datagrams as commands again.
#define KATHERINE_EMU_PX_CONFIG_WORDS   16384

// The readout applies the test pulse settings before acknowledging them,
// which takes about a second of its time.
#define KATHERINE_EMU_TP_APPLY_NS       1000000000ull

// Response datagrams the all-DAC scan answers with: one per scanned DAC,
// i.e. the chip's eighteen named DACs plus its four band-gap read-backs.
#define KATHERINE_EMU_DAC_SCAN_REPLIES  22

// Volts per register step of the emulated DAC scan. Synthetic, like the ADC
// ramp: the emulator models no analog front end, only the protocol.
#define KATHERINE_EMU_DAC_SCAN_VOLT     0.001f

// Data plane geometry.
#define KATHERINE_EMU_MATRIX_SIZE       256
#define KATHERINE_EMU_TICK_NS           25 /* readout timer period at 40 MHz */
#define KATHERINE_EMU_TOA_OFFSET_PERIOD 64 /* hits between timestamp offset MDs */
#define KATHERINE_EMU_SHAPE_BURST_BYTES 65536

// Measurement data headers, as recognized by the acquisition decoder.
#define KATHERINE_EMU_MD_PIXEL          0x4
#define KATHERINE_EMU_MD_TIME_OFFSET    0x5
#define KATHERINE_EMU_MD_NEW_FRAME      0x7
#define KATHERINE_EMU_MD_START_TIME_LSB 0x8
#define KATHERINE_EMU_MD_START_TIME_MSB 0x9
#define KATHERINE_EMU_MD_END_TIME_LSB   0xA
#define KATHERINE_EMU_MD_END_TIME_MSB   0xB
#define KATHERINE_EMU_MD_FRAME_FINISHED 0xC
#define KATHERINE_EMU_MD_LOST_PX        0xD
#define KATHERINE_EMU_MD_ABORTED        0xE

// Datagram fields are little-endian on the wire, as the readout writes
// them; the conversion is explicit so that the emulator produces the same
// bytes on either host byte order.
static inline uint32_t
katherine_emu_load_le32(const uint8_t *src)
{
    return (uint32_t) src[0] | ((uint32_t) src[1] << 8) | ((uint32_t) src[2] << 16) | ((uint32_t) src[3] << 24);
}

static inline void
katherine_emu_store_le(uint8_t *dst, uint64_t value, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        dst[i] = (uint8_t) (value >> (8 * i));
    }
}

// Splitmix64. Hand-rolled so that the generated streams do not depend on
// the host C library, which the determinism requirement rules out.
static inline uint64_t
katherine_emu_prng_next(katherine_emu_prng_t *prng)
{
    uint64_t z = (prng->state += 0x9E3779B97F4A7C15ull);
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline uint64_t
katherine_emu_prng_below(katherine_emu_prng_t *prng, uint64_t bound)
{
    // Modulo bias is irrelevant for synthetic hit patterns and keeps the
    // draw a single unconditional step, hence reproducible.
    if (bound == 0) return 0;
    return katherine_emu_prng_next(prng) % bound;
}

// Interfaces between the command responder (emulator.c) and the
// measurement data generator (emu_stream.c). They cross a translation unit
// boundary but not the library boundary, so they stay out of the ABI.

KATHERINE_NOT_EXPORTED void
katherine_emu_stream_reset(katherine_emu_t *emu);

KATHERINE_NOT_EXPORTED void
katherine_emu_stream_arm(katherine_emu_t *emu, uint8_t readout_mode);

KATHERINE_NOT_EXPORTED void
katherine_emu_stream_stop(katherine_emu_t *emu);

KATHERINE_NOT_EXPORTED void
katherine_emu_stream_advance(katherine_emu_t *emu, uint64_t ns);

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
