/**
 * \file
 * \brief A port held by another process is reported as such, not as a bad address.
 *
 * Every bind failure used to answer KATHERINE_E_ADDRESS, so a correct
 * address whose port was taken was indistinguishable from one typed wrong.
 * B4 measured four inputs through katherine_device_init() -- a malformed
 * host, an empty scheme, a 70-character host, and a good address with the
 * port held -- and got one code and one message for all four.
 *
 * KATHERINE_E_BUSY separates the last of those, and the distinction is
 * narrower than it looks. The sessions this library opens set SO_REUSEADDR,
 * and a second bind to the same port succeeds whenever every holder has set
 * it too. So the code reports a holder that did **not** -- another program,
 * or a stale socket -- while two libkatherine sessions on one host still
 * bind alongside each other. Both halves are pinned below, because the
 * second is what stops the code being read as more than it is.
 *
 * \author Petr Mánek
 * \date 26.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <katherine/katherine.h>

#include "transport/udp.h"

// transport/udp.h brings in <winsock2.h>, which does not declare inet_pton():
// that one lives in <ws2tcpip.h>, and without it MSVC takes it for an
// implicit int-returning function (C4013) and links it anyway.
#ifdef KATHERINE_WIN
#include <ws2tcpip.h>
#endif

#include "kports.h"
#include "ktest.h"

#define HOST       "127.0.0.1"
#define PORT_BUSY  KPORTS_PORT_BUSY
#define TIMEOUT_MS 100

#ifdef KATHERINE_WIN
typedef SOCKET holder_t;
#define HOLDER_NONE  INVALID_SOCKET
#define holder_close closesocket
#else
typedef int holder_t;
#define HOLDER_NONE  (-1)
#define holder_close close
#endif

// Winsock is reference-counted and kports_available() balances its own
// startup with a WSACleanup(), which takes the count back to zero. This
// file makes sockets of its own after that call, so it has to hold a
// reference for as long as it does -- without this every socket() here
// returns WSANOTINITIALISED and the holder below never binds.
static void
sockets_start(void)
{
#ifdef KATHERINE_WIN
    WSADATA wsa;
    (void) WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

static void
sockets_stop(void)
{
#ifdef KATHERINE_WIN
    (void) WSACleanup();
#endif
}

// A socket holding PORT_BUSY, with SO_REUSEADDR under the caller's control:
// that flag is the whole difference between the two cases below.
static holder_t
hold_port(bool reuseaddr)
{
    const holder_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == HOLDER_NONE) return HOLDER_NONE;

    if (reuseaddr) {
        int on = 1;
        (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));
    }

    // HOST rather than INADDR_ANY, which is the address the library will ask
    // for. On Windows the distinction decides the test: a wildcard holder and
    // a specific-address bind do not collide there, so a holder on INADDR_ANY
    // would leave the library's bind succeeding and prove nothing.
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT_BUSY);
    if (inet_pton(AF_INET, HOST, &addr.sin_addr) != 1) {
        holder_close(sock);
        return HOLDER_NONE;
    }

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        holder_close(sock);
        return HOLDER_NONE;
    }
    return sock;
}

static katherine_error_t
open_control(katherine_transport_t *transport)
{
    const katherine_udp_config_t control = {
        .local_address  = HOST,
        .local_port     = PORT_BUSY,
        .remote_address = HOST,
        .remote_port    = PORT_BUSY + 1,
        .timeout_ms     = TIMEOUT_MS,
    };
    return katherine_transport_init_udp(transport, &control);
}

// ------------------------------------------------------------------
// The control: with the port free the bind succeeds, so the refusal below
// is about the holder and not about the configuration.

static void
test_a_free_port_opens(void)
{
    if (!kports_available(PORT_BUSY, 1)) {
        printf("# SKIP port %d is taken\n", PORT_BUSY);
        return;
    }

    katherine_transport_t transport;
    KT_CHECK_EQ(open_control(&transport), KATHERINE_E_OK);
    katherine_transport_fini(&transport);
}

static void
test_a_foreign_holder_is_reported_busy(void)
{
    if (!kports_available(PORT_BUSY, 1)) {
        printf("# SKIP port %d is taken\n", PORT_BUSY);
        return;
    }

    const holder_t held = hold_port(false);
    KT_REQUIRE(held != HOLDER_NONE);

    katherine_transport_t transport;
    KT_CHECK_EQ(open_control(&transport), KATHERINE_E_BUSY);

    holder_close(held);
}

// The limit of the distinction, pinned so nobody reads more into the code
// than it carries -- and it is not the same limit on both platforms.
//
// On POSIX the library sets SO_REUSEADDR, so a holder that set it too is not
// a collision: both bind, and two libkatherine sessions are exactly that
// case. On Windows the library deliberately does not set it, so the address
// and port are its own: the holder's option makes no difference and a second
// session is refused. Measured on the build VM, both ways round.
//
// Which is to say KATHERINE_E_BUSY reaches further on Windows than on POSIX,
// and this is the case that says so.
static void
test_a_reuseaddr_holder_is_what_the_platform_says(void)
{
    if (!kports_available(PORT_BUSY, 1)) {
        printf("# SKIP port %d is taken\n", PORT_BUSY);
        return;
    }

    const holder_t held = hold_port(true);
    KT_REQUIRE(held != HOLDER_NONE);

    katherine_transport_t transport;
#ifdef KATHERINE_WIN
    KT_CHECK_EQ(open_control(&transport), KATHERINE_E_BUSY);
#else
    KT_CHECK_EQ(open_control(&transport), KATHERINE_E_OK);
    katherine_transport_fini(&transport);
#endif

    holder_close(held);
}

static void
test_the_code_renders(void)
{
    KT_CHECK_STR_EQ(katherine_strerror(KATHERINE_E_BUSY), "local port already in use");
}

int
main(void)
{
    sockets_start();

    KT_RUN(test_a_free_port_opens);
    KT_RUN(test_a_foreign_holder_is_reported_busy);
    KT_RUN(test_a_reuseaddr_holder_is_what_the_platform_says);
    KT_RUN(test_the_code_renders);

    const int rc = kt_summary();
    sockets_stop();
    return rc;
}
