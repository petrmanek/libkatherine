/**
 * \file
 * \brief The fixed UDP ports this suite binds, one block per test.
 *
 * Several tests need a loopback endpoint at a known number -- a mock readout
 * has to be somewhere its client can send to. Left to each test to pick, the
 * numbers collide: three tests shared 42620 and two more shared 42630 before
 * this header existed, which the suite survived only because ctest runs
 * serially by default. Under `ctest -j` the second binder of a port fails,
 * and the failure looks like a transport bug rather than a clash.
 *
 * So the numbers live here, eight per test, handed out in order. A new test
 * takes the next index and adds its assertion to the chain at the bottom.
 *
 * The daemon's own 1555/1556 are not here: they are the readout's documented
 * ports rather than a choice, which is why the tests that spawn it are
 * RUN_SERIAL instead.
 *
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/** Ports per block: enough for two client/mock pairs, which is the most any test uses. */
#define KPORTS_BLOCK_SIZE   8

/** First port of block n. High and uncommon, and well clear of the ephemeral range. */
#define KPORTS_BLOCK(n)     (42600 + KPORTS_BLOCK_SIZE * (n))

#define KPORTS_CMD_ENCODERS KPORTS_BLOCK(0)
#define KPORTS_CMD_TRANSACT KPORTS_BLOCK(1)
#define KPORTS_ISSUE16      KPORTS_BLOCK(2)
#define KPORTS_MD_DECODE    KPORTS_BLOCK(3)
#define KPORTS_GEN2_DECODE  KPORTS_BLOCK(4)
#define KPORTS_BIAS         KPORTS_BLOCK(5)
#define KPORTS_DAC_SCAN     KPORTS_BLOCK(6)
#define KPORTS_ACQ_UNIT     KPORTS_BLOCK(7)
#define KPORTS_TDC          KPORTS_BLOCK(8)
#define KPORTS_UDP_RCVBUF   KPORTS_BLOCK(9)
#define KPORTS_UDP_PINNING  KPORTS_BLOCK(10)
#define KPORTS_BENCH_UDP    KPORTS_BLOCK(11)

// The blocks must not overlap, which for blocks handed out in order is the
// same as each starting where the last ends. Stated rather than trusted: the
// definitions above are one edit away from two tests sharing an index, and
// that edit is exactly the mistake this header exists to prevent.
_Static_assert(KPORTS_CMD_TRANSACT >= KPORTS_CMD_ENCODERS + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_ISSUE16 >= KPORTS_CMD_TRANSACT + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_MD_DECODE >= KPORTS_ISSUE16 + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_GEN2_DECODE >= KPORTS_MD_DECODE + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_BIAS >= KPORTS_GEN2_DECODE + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_DAC_SCAN >= KPORTS_BIAS + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_ACQ_UNIT >= KPORTS_DAC_SCAN + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_TDC >= KPORTS_ACQ_UNIT + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_UDP_RCVBUF >= KPORTS_TDC + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_UDP_PINNING >= KPORTS_UDP_RCVBUF + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_BENCH_UDP >= KPORTS_UDP_PINNING + KPORTS_BLOCK_SIZE, "port blocks overlap");
