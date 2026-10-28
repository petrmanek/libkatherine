/**
 * \file
 * \brief The frame tail the emulator sends, per generation and per chip.
 *
 * A frame's start and end timestamps arrive as four words. On the second
 * generation the readout sends the whole set once per chip, after the hits,
 * and names the chip in the first of the four -- ARM_APP/main.c's
 * sendFrameTail(). The emulator sent one set whatever chip_count said, so
 * nothing in this suite could see a two-chip frame, which is the shape the
 * readout at 192.168.1.43 has.
 *
 * This drives the generator directly: no sockets, no ksim, a virtual clock
 * advanced by hand, and katherine_emu_data_out() for the words. What it
 * asserts is the stream's shape, not what the library makes of it -- the
 * library keeps one katherine_frame_info_t and the last chip's tail wins,
 * which is recorded above the timestamp handlers and is a decision rather
 * than a defect to fix here.
 *
 * \author Petr Mánek
 * \date 28.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include <katherine/katherine.h>
#include <katherine/emulator.h>

#include "emu/emu.h"
#include "protocol/md.h"
#include "ktest.h"

// Hardware types, as the capability table names them.
#define HW_GEN1        0x01
#define HW_GEN2        0x03

// Ten-nanosecond units, which is what the acquisition time registers count.
#define FRAME_UNITS    1000
#define HITS_PER_FRAME 4

// Comfortably past the one frame, so every stage is due at once.
#define ADVANCE_NS     1000000

#define MAX_WORDS      512

typedef struct stream_dump {
    uint64_t word[MAX_WORDS];
    size_t words;
} stream_dump_t;

// Every word the generator has to offer, in order, as whole 48-bit data.
static void
dump(katherine_emu_t *emu, stream_dump_t *out)
{
    uint8_t buf[KATHERINE_EMU_STAGE_MDS * KATHERINE_EMU_MD_SIZE];
    size_t len;

    out->words = 0;
    while (katherine_emu_data_out(emu, buf, sizeof(buf), &len) == 0) {
        for (size_t off = 0; off + KATHERINE_EMU_MD_SIZE <= len; off += KATHERINE_EMU_MD_SIZE) {
            uint64_t word = 0;
            for (size_t i = 0; i < KATHERINE_EMU_MD_SIZE; ++i) {
                word |= (uint64_t) buf[off + i] << (8 * i);
            }
            if (out->words < MAX_WORDS) out->word[out->words] = word;
            ++out->words;
        }
    }
    KT_REQUIRE(out->words <= MAX_WORDS);
}

// One frame of one acquisition, from a profile this names.
static void
one_frame(uint8_t hw_type, uint8_t chip_count, stream_dump_t *out)
{
    katherine_emu_profile_t profile;
    katherine_emu_t emu;

    katherine_emu_profile_defaults(&profile);
    profile.hw_type        = hw_type;
    profile.chip_count     = chip_count;
    profile.hits_per_frame = HITS_PER_FRAME;
    profile.lost_per_frame = 0;

    KT_REQUIRE(katherine_emu_init(&emu, &profile) == 0);

    emu.regs.acq_time_lsb = FRAME_UNITS;
    emu.regs.acq_time_msb = 0;
    emu.regs.frame_count  = 1;

    katherine_emu_stream_arm(&emu, KATHERINE_TPX3_READOUT_SEQUENTIAL);
    katherine_emu_advance(&emu, ADVANCE_NS);

    dump(&emu, out);
    katherine_emu_fini(&emu);
}

static size_t
count_header(const stream_dump_t *d, uint8_t header)
{
    size_t n = 0;
    for (size_t i = 0; i < d->words; ++i) {
        if (EXTRACT(d->word[i], md, header) == header) ++n;
    }
    return n;
}

// Index of the i-th word carrying this header, or d->words if there is none.
static size_t
find_header(const stream_dump_t *d, uint8_t header, size_t which)
{
    size_t seen = 0;
    for (size_t i = 0; i < d->words; ++i) {
        if (EXTRACT(d->word[i], md, header) != header) continue;
        if (seen == which) return i;
        ++seen;
    }
    return d->words;
}

static void
test_gen2_sends_one_tail_per_chip(void)
{
    stream_dump_t d;
    one_frame(HW_GEN2, 2, &d);

    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_START_TIME_LSB), 2u);
    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_START_TIME_MSB), 2u);
    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_END_TIME_LSB), 2u);
    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_END_TIME_MSB), 2u);

    // The first word of each set names its chip, and only that word does.
    const size_t first  = find_header(&d, KATHERINE_EMU_MD_START_TIME_LSB, 0);
    const size_t second = find_header(&d, KATHERINE_EMU_MD_START_TIME_LSB, 1);
    KT_REQUIRE(second < d.words);
    KT_CHECK_EQ(EXTRACT(d.word[first], md_time_lsb, chip), 0u);
    KT_CHECK_EQ(EXTRACT(d.word[second], md_time_lsb, chip), 1u);

    // The four of one chip arrive together, in the firmware's order.
    KT_CHECK_EQ(EXTRACT(d.word[first + 1], md, header), KATHERINE_EMU_MD_START_TIME_MSB);
    KT_CHECK_EQ(EXTRACT(d.word[first + 2], md, header), KATHERINE_EMU_MD_END_TIME_LSB);
    KT_CHECK_EQ(EXTRACT(d.word[first + 3], md, header), KATHERINE_EMU_MD_END_TIME_MSB);
    KT_CHECK_EQ(second, first + 4);

    // And the whole tail follows the hits, where the first generation's
    // start timestamps precede them.
    const size_t new_frame = find_header(&d, KATHERINE_EMU_MD_NEW_FRAME, 0);
    KT_CHECK(new_frame < first);
    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_FRAME_FINISHED), 1u);
    KT_CHECK(find_header(&d, KATHERINE_EMU_MD_FRAME_FINISHED, 0) > second);
}

// One chip is one tail, which is every readout this suite had before.
static void
test_one_chip_is_one_tail(void)
{
    stream_dump_t d;
    one_frame(HW_GEN2, 1, &d);

    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_START_TIME_LSB), 1u);
    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_END_TIME_MSB), 1u);
    KT_CHECK_EQ(EXTRACT(d.word[find_header(&d, KATHERINE_EMU_MD_START_TIME_LSB, 0)], md_time_lsb, chip), 0u);
}

// The first generation carries one chip and no chip index, and its order is
// unsourced -- no firmware for it is on this machine -- so it is left as it
// was: the frame opens, its start is timed, the hits follow.
static void
test_gen1_keeps_its_order(void)
{
    stream_dump_t d;
    one_frame(HW_GEN1, 1, &d);

    KT_CHECK_EQ(count_header(&d, KATHERINE_EMU_MD_START_TIME_LSB), 1u);

    const size_t start = find_header(&d, KATHERINE_EMU_MD_START_TIME_LSB, 0);
    const size_t end   = find_header(&d, KATHERINE_EMU_MD_END_TIME_LSB, 0);
    KT_REQUIRE(end < d.words);
    KT_CHECK(start < end);

    // A hit sits between them, which is what "the tail does not follow the
    // hits here" means in one assertion.
    bool hit_between = false;
    for (size_t i = start; i < end; ++i) {
        if (EXTRACT(d.word[i], md, header) == 0x4) hit_between = true;
    }
    KT_CHECK(hit_between);
}

int
main(void)
{
    KT_RUN(test_gen2_sends_one_tail_per_chip);
    KT_RUN(test_one_chip_is_one_tail);
    KT_RUN(test_gen1_keeps_its_order);
    return kt_summary();
}
