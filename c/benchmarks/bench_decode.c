/**
 * \file
 * \brief Decode throughput benchmark (B1): Mhits/s per pixel variant.
 *
 * Builds one canned in-RAM buffer of six-byte pixel measurement data (header
 * 0x4), then walks it with the exact per-hit work c/src/acquisition.c's
 * DEFINE_ACQ_IMPL read loop does for each of the six pixel variants: a header
 * check, the variant's field EXTRACTs (through its pmd_*_map()), a struct
 * store into a wraparound pixel slot, and the pixel-buffer-full bookkeeping
 * that would otherwise trigger a flush -- approximated here, as in the
 * planning-phase toy model (misc/bench-seeds/toy_dispatch_bench.c), by simply
 * rewinding the slot index, since there is no handler to flush to.
 *
 * This file reaches into the library's private headers ("md.h" and
 * friends) through the katherine_private interface target. That is
 * deliberate: a benchmark is in-tree tooling built against the library's
 * internals to measure them, not user code bound by the public API, so the
 * private surface is available to it the way it is to c/tests.
 *
 * Every variant's Mhits/s and a reference decode_memcpy figure (a plain
 * memcpy of the same buffer, normalized to hits) are printed as one JSON
 * line each; a "# ..." comment line with the run's checksum follows each
 * one, so that two runs over the same buffer can be compared for
 * reproducibility without perturbing the JSON stream bench_compare.py reads.
 * decode_memcpy is the self-normalizing reference tools/bench_compare.py's
 * --ratios mode uses: the ratio of a variant to it is runner-independent
 * even though the absolute Mhit/s figures vary by a factor of ~2 across
 * shared CI runner generations.
 *
 * \author Petr Mánek
 * \date 24.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/acquisition.h>
#include <katherine/toa.h>

// Internal headers, provided by the katherine_private interface target --
// see the file comment. md.h pulls in "bitfields.h" (EXTRACT/INSERT)
// itself, the same way it does for c/src/acquisition.c; monoclock.h wraps
// the platform monotonic clock.
#include "protocol/md.h"
#include "monoclock.h"

// Size of the canned buffer, in whole six-byte words: several MB, comfortably
// past L1/L2 but not so large a run wastes time or memory.
#define BENCH_BUFFER_MB   8u

// Per-variant wraparound pixel slot, sized in bytes like the library's own
// pixel_buffer_size is (see katherine_acquisition_init()); its capacity in
// items is this divided by the variant's own struct size. Matches the
// 65536-byte slot of the validated toy model.
#define BENCH_SLOT_BYTES  65536u

// Every benchmark keeps re-running its loop until it has measured at least
// this many seconds, so that short variants still get a stable rate. The
// KATHERINE_BENCH_MIN_SECONDS environment variable overrides the default.
// The default comes from a duration sweep on the recording host: medians
// are stable from 0.2 s and the run-to-run spread only settles into the
// few-percent regime from about 1 s, so twice that is used for margin.
#define BENCH_MIN_SECONDS 2.0

static double g_min_seconds = BENCH_MIN_SECONDS;

static volatile uint64_t g_sink;

static double
now_s(void)
{
    return 1e-9 * (double) katherine_clock_monotonic_ns();
}

// xorshift64: fast, deterministic, and good enough to scatter every field
// read back below without needing a "real" PRNG or any external state. The
// seed is a fixed constant, so successive runs decode the exact same buffer
// and land on the exact same checksums.
static uint64_t
next_rand(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

// Fills `words` consecutive six-byte pixel measurement data, header 0x4, with
// deterministic pseudo-random field values built through the very
// INSERT()/_BITS_pmd_* triads md.h's decoders read them back with -- the
// same principle c/tests/test_md_decode.c uses, so buffer and decoder can
// never disagree. The toa_tot field names are used to reach the full 44
// non-header bits of the word; every other variant below reads some subset
// of those same bit positions, exactly as the real hardware's fixed wire
// layout is reinterpreted differently per acquisition mode. Written and
// later read back with memcpy() of a host-native uint64_t rather than a
// manual byte order, so the benchmark is self-consistent regardless of host
// endianness -- it never leaves this process, so wire byte order does not
// apply.
static void
build_canned_buffer(uint8_t *buf, size_t words)
{
    uint64_t seed = 0x9E3779B97F4A7C15ull;

    for (size_t i = 0; i < words; ++i) {
        uint64_t r  = next_rand(&seed);
        uint64_t md = INSERT((uint64_t) 0, md, header, (uint64_t) 0x4);
        md          = INSERT(md, pmd_toa_tot, coord_x, r);
        md          = INSERT(md, pmd_toa_tot, coord_y, r >> 8);
        md          = INSERT(md, pmd_toa_tot, toa, r >> 16);
        md          = INSERT(md, pmd_toa_tot, tot, r >> 30);
        md          = INSERT(md, pmd_toa_tot, hit_count, r >> 40);
        memcpy(buf + i * KATHERINE_MD_SIZE, &md, KATHERINE_MD_SIZE);
    }
}

// One pass over the canned buffer for pixel variant SUFFIX: the same
// dispatch, EXTRACTs and struct store as acquisition_read_##SUFFIX()'s inner
// loop, minus the transport and the flush handler -- there is no data source
// or consumer here, only the decode itself. CHECKSUM sums the fields
// pmd_##SUFFIX##_map() just wrote, so the store cannot be optimized into a
// dead one; the running total is folded into the volatile g_sink exactly
// once per pass rather than per hit, so the sink itself never becomes the
// bottleneck it exists to prevent.
#define DEFINE_BENCH_DECODE(SUFFIX, MAP, CHECKSUM) \
    static void \
    bench_decode_##SUFFIX(const uint8_t *buf, size_t words, uint64_t *out_checksum) \
    { \
        static katherine_px_##SUFFIX##_t slot[BENCH_SLOT_BYTES / sizeof(katherine_px_##SUFFIX##_t)]; \
        const size_t max_valid      = sizeof(slot) / sizeof(slot[0]); \
        size_t valid                = 0; \
        katherine_acquisition_t acq = {0}; \
        /* Zero would let the compiler fold the coarse-tick multiply away and \
           measure a decoder that does less work than the real one. */ \
        acq.toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(FREQ_40); \
        acq.last_toa_offset               = katherine_tpx3_toa_coarse_tick_to_fine_ticks(FREQ_40); \
        uint64_t acc                      = 0; \
        const uint8_t *p                  = buf; \
        for (size_t i = 0; i < words; ++i, p += KATHERINE_MD_SIZE) { \
            uint64_t word; \
            /* A fixed-size memcpy is the strict-aliasing-safe spelling of \
               the same unaligned 8-byte load the library's read loop does \
               with a raw cast: the compiler folds it to a single mov, and \
               the built object contains no memcpy call (checked in the \
               disassembly), so the measured work is identical. */ \
            memcpy(&word, p, sizeof(word)); \
            char hdr = EXTRACT(word, md, header); \
            if (hdr == 0x4) { \
                if (valid == max_valid) valid = 0; \
                katherine_px_##SUFFIX##_t *dst = &slot[valid]; \
                MAP(dst, &word, &acq); \
                acc += (CHECKSUM); \
                ++valid; \
            } else { \
                /* Never taken by this canned buffer (every word carries the \
                   pixel header): kept so the branch the real read loop takes \
                   on every datum is still here to predict. */ \
                acc += (uint64_t) hdr; \
            } \
        } \
        g_sink        = acc; \
        *out_checksum = acc; \
    }

// clang-format off
DEFINE_BENCH_DECODE(f_toa_tot, pmd_f_toa_tot_s4_map,    (uint64_t) dst->coord.x + dst->coord.y + dst->tot + dst->timestamp)
DEFINE_BENCH_DECODE(toa_tot, pmd_toa_tot_s4_map,      (uint64_t) dst->coord.x + dst->coord.y + dst->timestamp + dst->hit_count + dst->tot)
DEFINE_BENCH_DECODE(f_toa_only, pmd_f_toa_only_s4_map,   (uint64_t) dst->coord.x + dst->coord.y + dst->timestamp)
DEFINE_BENCH_DECODE(toa_only, pmd_toa_only_s4_map,     (uint64_t) dst->coord.x + dst->coord.y + dst->timestamp + dst->hit_count)
DEFINE_BENCH_DECODE(f_event_itot, pmd_f_event_itot_map, (uint64_t) dst->coord.x + dst->coord.y + dst->event_count + dst->integral_tot)
DEFINE_BENCH_DECODE(event_itot, pmd_event_itot_map,   (uint64_t) dst->coord.x + dst->coord.y + dst->hit_count + dst->event_count + dst->integral_tot)
// clang-format on

#undef DEFINE_BENCH_DECODE

typedef void (*bench_decode_fn)(const uint8_t *, size_t, uint64_t *);

// Reference figure: a plain memcpy() of the same buffer, normalized to hits
// like every decode_* figure is, so that a ratio of the two is meaningful.
// Reads back one byte per page-ish stride of the copy into the checksum, an
// observable use of the destination that keeps the copy from being proven
// dead.
static uint8_t *g_memcpy_scratch;

static void
bench_decode_memcpy(const uint8_t *buf, size_t words, uint64_t *out_checksum)
{
    size_t bytes = words * KATHERINE_MD_SIZE;
    uint64_t acc = 0;

    memcpy(g_memcpy_scratch, buf, bytes);
    for (size_t i = 0; i < bytes; i += 4096) {
        acc += g_memcpy_scratch[i];
    }

    g_sink        = acc;
    *out_checksum = acc;
}

// Runs `fn` over the buffer repeatedly until at least BENCH_MIN_SECONDS have
// elapsed, then reports one {"bench":...} JSON line with the achieved
// Mhit/s, followed by a "# " comment line naming the checksum of the last
// pass -- informational only, and deliberately not JSON, so a JSON-line
// reader can skip it unharmed.
static void
run_bench(const char *name, bench_decode_fn fn, const uint8_t *buf, size_t words)
{
    uint64_t iterations = 0;
    uint64_t checksum   = 0;
    double t0           = now_s();
    double dt;

    do {
        fn(buf, words, &checksum);
        ++iterations;
        dt = now_s() - t0;
    } while (dt < g_min_seconds);

    uint64_t total_words = iterations * (uint64_t) words;
    double mhits         = (double) total_words / dt / 1e6;

    printf("{\"bench\":\"decode_%s\",\"value\":%.2f,\"unit\":\"Mhit/s\"}\n", name, mhits);
    printf("# decode_%s checksum 0x%016llx over %llu word(s), %llu pass(es), %.3f s\n", name,
        (unsigned long long) checksum, (unsigned long long) total_words, (unsigned long long) iterations, dt);
    fflush(stdout);
}

int
main(void)
{
    const char *env = getenv("KATHERINE_BENCH_MIN_SECONDS");
    if (env != NULL) {
        double v = strtod(env, NULL);
        if (v > 0) g_min_seconds = v;
    }

    size_t words = ((size_t) BENCH_BUFFER_MB << 20) / KATHERINE_MD_SIZE;
    size_t bytes = words * KATHERINE_MD_SIZE;

    // One extra word past the last whole one: the loop above reads each word
    // through an 8-byte memcpy() (matching the unaligned uint64_t load the
    // real read loop performs), so the last word's load reaches 2 bytes past
    // the canned data -- exactly the margin katherine_acquisition_init()
    // reserves on the real md_buffer for the same reason.
    uint8_t *buf = (uint8_t *) malloc(bytes + sizeof(uint64_t));
    if (buf == NULL) return 1;
    build_canned_buffer(buf, words);

    g_memcpy_scratch = (uint8_t *) malloc(bytes);
    if (g_memcpy_scratch == NULL) {
        free(buf);
        return 1;
    }

    run_bench("f_toa_tot", bench_decode_f_toa_tot, buf, words);
    run_bench("toa_tot", bench_decode_toa_tot, buf, words);
    run_bench("f_toa_only", bench_decode_f_toa_only, buf, words);
    run_bench("toa_only", bench_decode_toa_only, buf, words);
    run_bench("f_event_itot", bench_decode_f_event_itot, buf, words);
    run_bench("event_itot", bench_decode_event_itot, buf, words);
    run_bench("memcpy", bench_decode_memcpy, buf, words);

    free(g_memcpy_scratch);
    free(buf);
    return 0;
}
