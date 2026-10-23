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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <katherine/global.h>

#ifdef KATHERINE_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/** Ports per block: enough for two client/mock pairs, which is the most any test uses. */
#define KPORTS_BLOCK_SIZE    8

/**
 * First port of block n.
 *
 * Below the ephemeral range rather than above it. The previous base, 42600,
 * sat inside Linux's default net.ipv4.ip_local_port_range of 32768-60999, so
 * the kernel could hand any of these ports to any program on the machine --
 * and did: a browser held KPORTS_DISCOVER and test_discover failed as though
 * the library were broken. 20223 is clear of /etc/services and twelve
 * thousand ports clear of the ephemeral floor.
 *
 * No fixed port is collision-proof, which is what kports_available() below is
 * for.
 */
#define KPORTS_BLOCK(n)      (20223 + KPORTS_BLOCK_SIZE * (n))

#define KPORTS_CMD_ENCODERS  KPORTS_BLOCK(0)
#define KPORTS_CMD_TRANSACT  KPORTS_BLOCK(1)
#define KPORTS_ISSUE16       KPORTS_BLOCK(2)
#define KPORTS_MD_DECODE     KPORTS_BLOCK(3)
#define KPORTS_GEN2_DECODE   KPORTS_BLOCK(4)
#define KPORTS_BIAS          KPORTS_BLOCK(5)
#define KPORTS_CHIP_READBACK KPORTS_BLOCK(6)
#define KPORTS_ACQ_UNIT      KPORTS_BLOCK(7)
#define KPORTS_TDC           KPORTS_BLOCK(8)
#define KPORTS_UDP_RCVBUF    KPORTS_BLOCK(9)
#define KPORTS_UDP_PINNING   KPORTS_BLOCK(10)
#define KPORTS_BENCH_UDP     KPORTS_BLOCK(11)
#define KPORTS_DISCOVER      KPORTS_BLOCK(12)
#define KPORTS_TRANSPORT     KPORTS_BLOCK(13)
#define KPORTS_FINI          KPORTS_BLOCK(14)
#define KPORTS_SET_DAC       KPORTS_BLOCK(15)
#define KPORTS_STATUS_NULL   KPORTS_BLOCK(16)
#define KPORTS_REPR          KPORTS_BLOCK(17)
#define KPORTS_COMPAT1       KPORTS_BLOCK(18)

// The blocks must not overlap, which for blocks handed out in order is the
// same as each starting where the last ends. Stated rather than trusted: the
// definitions above are one edit away from two tests sharing an index, and
// that edit is exactly the mistake this header exists to prevent.
_Static_assert(KPORTS_CMD_TRANSACT >= KPORTS_CMD_ENCODERS + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_ISSUE16 >= KPORTS_CMD_TRANSACT + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_MD_DECODE >= KPORTS_ISSUE16 + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_GEN2_DECODE >= KPORTS_MD_DECODE + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_BIAS >= KPORTS_GEN2_DECODE + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_CHIP_READBACK >= KPORTS_BIAS + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_ACQ_UNIT >= KPORTS_CHIP_READBACK + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_TDC >= KPORTS_ACQ_UNIT + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_UDP_RCVBUF >= KPORTS_TDC + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_UDP_PINNING >= KPORTS_UDP_RCVBUF + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_BENCH_UDP >= KPORTS_UDP_PINNING + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_DISCOVER >= KPORTS_BENCH_UDP + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_TRANSPORT >= KPORTS_DISCOVER + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_FINI >= KPORTS_TRANSPORT + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_SET_DAC >= KPORTS_FINI + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_STATUS_NULL >= KPORTS_SET_DAC + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_REPR >= KPORTS_STATUS_NULL + KPORTS_BLOCK_SIZE, "port blocks overlap");
_Static_assert(KPORTS_COMPAT1 >= KPORTS_REPR + KPORTS_BLOCK_SIZE, "port blocks overlap");

// True where every port of a block can be bound right now. A fixed port is
// always somebody else's to take first, and a test that cannot have the one
// it reserved has learned nothing about the library -- so it skips rather
// than reporting a failure that is not there.
//
// The probe sockets are closed before returning, so this answers for the
// moment it was asked and no longer. That is the same race every bind has,
// and it turns a certain false failure into an unlikely one.
static inline bool
kports_available(uint16_t first, unsigned count)
{
#ifdef KATHERINE_WIN
    // Winsock needs starting before a socket can be made.
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    bool available = true;

    for (unsigned i = 0; i < count && available; ++i) {
#ifdef KATHERINE_WIN
        const SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_SOCKET) break;
#else
        const int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) break;

        // The library sets this, so the probe must too or it answers a
        // different question than the one that will be asked.
        int reuse = 1;
        (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons((uint16_t) (first + i));

        available = bind(sock, (struct sockaddr *) &addr, sizeof(addr)) == 0;

#ifdef KATHERINE_WIN
        (void) closesocket(sock);
#else
        (void) close(sock);
#endif
    }

#ifdef KATHERINE_WIN
    (void) WSACleanup();
#endif

    return available;
}
