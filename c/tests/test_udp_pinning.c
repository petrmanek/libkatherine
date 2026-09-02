/**
 * @file
 * @brief A pinned UDP session keeps the remote address it was given.
 *
 * Every datagram a session receives used to make its sender the session's
 * remote address, so a single stray one -- a late response of a readout
 * probed earlier, a scan, a command a loopback address nobody is bound to
 * delivered back to its own sender -- retargeted the session for good
 * (issue #23, "net: stop stray datagrams retargeting remote addr"). A client
 * pins its sessions against that (katherine_udp_pin_remote()), while a
 * server, whose replies are meant for whoever asked last, keeps the original
 * behavior; both halves of that contract are asserted below, so that a
 * regression in either direction fails a case here.
 *
 * Three endpoints, all over loopback and all driven through the public
 * katherine_udp_* API alone -- no daemon, no raw sockets, nothing that binds
 * the fixed ports of a device session: A is the client under test, B the
 * peer it addresses, and C the stray source. A and B bind the primary
 * loopback address explicitly instead of the wildcard, so that the source
 * address of everything they send is decided here rather than by the routing
 * table.
 *
 * C sits on a second local address for the cases about datagrams *discarded*
 * as foreign, and shares A's and B's address for the cases about where a
 * session *sends*: the pin compares the remote host and deliberately not its
 * port, because a readout answers commands from its command port but streams
 * measurement data from another. A host that has only one loopback address
 * runs the second group and skips the first.
 *
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <katherine/error.h>
#include <katherine/udp.h>

#include "ktest.h"

// ------------------------------------------------------------------
// Addressing. The ports are uncommon and high on purpose: this test claims
// no global resource the way the fixed 1555/1556 of a device session are
// one, so it needs no exclusive slot among the registered tests.

#define PORT_A          42555
#define PORT_B          42556
#define PORT_C          42557

// The primary loopback address, and a second local one for the endpoints
// that must appear as another host. All of 127.0.0.0/8 is local on Linux,
// and on macOS once aliased; where the second address cannot be bound, the
// cases that need it are skipped.
#define HOST_LOCAL      "127.0.0.1"
#define HOST_OTHER      "127.0.0.2"

// Receive timeout of the endpoints. Generous for a datagram already in
// flight over loopback, short enough that the cases expecting nothing to
// arrive stay quick.
#define TIMEOUT_MS      200

// The discard-bound case instead wants a timeout it can outlive: the receive
// under test must return once its discard budget is spent, well before the
// timeout expires, and time(NULL) resolves whole seconds only.
#define LONG_TIMEOUT_MS 3000
#define BOUND_BUDGET_S  2.0

// Payloads. Distinct, equally long and text, so that a misdelivered one is
// named in the failure rather than merely counted; the two halves are what
// the recv_exact() case reassembles.
#define TEXT_STRAY      "stray"
#define TEXT_WANTED     "wanted"
#define TEXT_REPLY      "reply"
#define TEXT_HALF       "half"
#define TEXT_REST       "rest"

// ------------------------------------------------------------------
// Endpoints and expectations.

typedef struct endpoints {
    katherine_udp_t a; /* the session under test, addressing B */
    katherine_udp_t b; /* the peer A addresses */
    katherine_udp_t c; /* the stray source, sending to A unasked */
} endpoints_t;

// Brings up the three endpoints, A pinned if asked and with the given
// receive timeout. The stray source binds stray_host, which decides whether
// it appears to A as another host or as another port of A's own peer.
static int
endpoints_init(endpoints_t *e, const char *stray_host, bool pin, uint32_t timeout_ms)
{
    int res;

    if ((res = katherine_udp_init_bound(&e->a, HOST_LOCAL, PORT_A, HOST_LOCAL, PORT_B, timeout_ms)) != 0) {
        goto err_a;
    }

    if (pin) {
        katherine_udp_pin_remote(&e->a);
    }

    if ((res = katherine_udp_init_bound(&e->b, HOST_LOCAL, PORT_B, HOST_LOCAL, PORT_A, TIMEOUT_MS)) != 0) {
        goto err_b;
    }

    if ((res = katherine_udp_init_bound(&e->c, stray_host, PORT_C, HOST_LOCAL, PORT_A, TIMEOUT_MS)) != 0) {
        goto err_c;
    }

    return 0;

err_c:
    katherine_udp_fini(&e->b);
err_b:
    katherine_udp_fini(&e->a);
err_a:
    return res;
}

static void
endpoints_fini(endpoints_t *e)
{
    katherine_udp_fini(&e->c);
    katherine_udp_fini(&e->b);
    katherine_udp_fini(&e->a);
}

// True if the second local address can be bound here, i.e. whether this host
// can speak from an address other than the primary loopback one.
static bool
host_other_available(void)
{
    katherine_udp_t probe;

    if (katherine_udp_init_bound(&probe, HOST_OTHER, PORT_C, HOST_LOCAL, PORT_A, TIMEOUT_MS) != 0) {
        return false;
    }

    katherine_udp_fini(&probe);
    return true;
}

// True if res is the code an expired receive timeout yields, which is also
// what a pinned receive reports once its discard budget is spent.
static bool
is_timeout(int res)
{
    return res == -KATHERINE_E_TIMEOUT;
}

static void
send_text(katherine_udp_t *u, const char *text)
{
    KT_CHECK_EQ(katherine_udp_send_exact(u, text, strlen(text)), 0);
}

// Requires the next datagram of u to carry exactly text.
static void
expect_text(katherine_udp_t *u, const char *text)
{
    char buf[64];
    size_t count = sizeof(buf);
    int res      = katherine_udp_recv(u, buf, &count);

    KT_CHECK_EQ(res, 0);
    if (res != 0) return;

    KT_CHECK_EQ(count, strlen(text));
    if (count != strlen(text)) return;

    KT_CHECK_MEM_EQ(buf, text, count);
}

// Requires u to receive nothing at all: the timeout expires and no payload
// is handed over.
static void
expect_timeout(katherine_udp_t *u)
{
    char buf[64];
    size_t count = sizeof(buf);

    KT_CHECK(is_timeout(katherine_udp_recv(u, buf, &count)));
}

// ------------------------------------------------------------------
// a) The heart of the fix: a stray datagram does not move where a pinned
// session sends. The stray comes from A's own host here, so a pinned
// session still delivers it -- host, not port, is what the pin compares
// -- and what must not happen is A's next message going anywhere but B.
// Before the fix, that message went to C and B waited in vain.

static void
test_pinned_keeps_send_target(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_LOCAL, true, TIMEOUT_MS) == 0);

    send_text(&e.c, TEXT_STRAY);
    expect_text(&e.a, TEXT_STRAY);

    send_text(&e.a, TEXT_REPLY);
    expect_text(&e.b, TEXT_REPLY);
    expect_timeout(&e.c);

    endpoints_fini(&e);
}

// b) The server contract, which the ksim daemon depends on and which is the
// behavior of every session that is not pinned: the sender of the last
// datagram received becomes the remote, so the next message goes back to
// whoever asked. Asserted here so that the fix above cannot quietly
// spread to the sessions that need this.

static void
test_unpinned_follows_sender(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_LOCAL, false, TIMEOUT_MS) == 0);

    send_text(&e.c, TEXT_STRAY);
    expect_text(&e.a, TEXT_STRAY);

    send_text(&e.a, TEXT_REPLY);
    expect_text(&e.c, TEXT_REPLY);
    expect_timeout(&e.b);

    endpoints_fini(&e);
}

// c) A datagram from another host is discarded and the wait continues inside
// the same call, which therefore reports the very timeout it would have
// reported had the stray never arrived. The session still addresses B.

static void
test_pinned_discards_other_host(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_OTHER, true, TIMEOUT_MS) == 0);

    send_text(&e.c, TEXT_STRAY);
    expect_timeout(&e.a);

    send_text(&e.a, TEXT_REPLY);
    expect_text(&e.b, TEXT_REPLY);
    expect_timeout(&e.c);

    endpoints_fini(&e);
}

// d) Stepping over a stray does not cost the peer's own datagram: it is
// still there to be received, in the same call that discarded the stray
// ahead of it.

static void
test_pinned_receives_after_stray(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_OTHER, true, TIMEOUT_MS) == 0);

    send_text(&e.c, TEXT_STRAY);
    send_text(&e.b, TEXT_WANTED);
    expect_text(&e.a, TEXT_WANTED);

    endpoints_fini(&e);
}

// e) A multi-datagram message is verified datagram by datagram: an eight-byte
// message arrives as two halves of the peer's with a stray landing in
// between, and comes out whole. The three sends are sequential calls of
// one thread, and loopback delivery completes within the call, so the
// order they queue up in is the order they were sent in.

static void
test_pinned_recv_exact_skips_stray(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_OTHER, true, TIMEOUT_MS) == 0);

    send_text(&e.b, TEXT_HALF);
    send_text(&e.c, TEXT_STRAY);
    send_text(&e.b, TEXT_REST);

    char got[8];
    KT_CHECK_EQ(katherine_udp_recv_exact(&e.a, got, sizeof(got)), 0);
    KT_CHECK_MEM_EQ(got, TEXT_HALF TEXT_REST, sizeof(got));

    endpoints_fini(&e);
}

// f) The discard budget bounds the call. Every discarded datagram rearms the
// socket's receive timeout, so a source chatty enough would stretch a
// single call for as long as it kept sending; after
// KATHERINE_UDP_PIN_MAX_DISCARDS of them the receive gives up instead.
// The flood is longer than the budget, so the call must come back well
// before its (deliberately long) timeout would expire, and the datagrams
// it did not reach must still be queued -- which is what the receive
// afterwards proves by finding the peer's own datagram behind them.

static void
test_pinned_discard_bound(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_OTHER, true, LONG_TIMEOUT_MS) == 0);

    for (uint32_t i = 0; i < KATHERINE_UDP_PIN_MAX_DISCARDS + 8; ++i) {
        send_text(&e.c, TEXT_STRAY);
    }

    char buf[64];
    size_t count   = sizeof(buf);
    time_t started = time(NULL);
    int res        = katherine_udp_recv(&e.a, buf, &count);
    double elapsed = difftime(time(NULL), started);

    KT_CHECK(is_timeout(res));
    KT_CHECK(elapsed <= BOUND_BUDGET_S);

    send_text(&e.b, TEXT_WANTED);
    expect_text(&e.a, TEXT_WANTED);

    endpoints_fini(&e);
}

// g) Repointing a pinned session moves the pin with it: from then on the
// newly named host is the one whose datagrams are accepted, and the one
// the session sends to. The peer it used to address is a stray source
// like any other.

static void
test_set_remote_moves_pin(void)
{
    endpoints_t e;
    KT_REQUIRE(endpoints_init(&e, HOST_OTHER, true, TIMEOUT_MS) == 0);

    KT_CHECK_EQ(katherine_udp_set_remote(&e.a, HOST_OTHER, PORT_C), 0);

    send_text(&e.b, TEXT_STRAY);
    expect_timeout(&e.a);

    send_text(&e.c, TEXT_WANTED);
    expect_text(&e.a, TEXT_WANTED);

    send_text(&e.a, TEXT_REPLY);
    expect_text(&e.c, TEXT_REPLY);
    expect_timeout(&e.b);

    endpoints_fini(&e);
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_pinned_keeps_send_target);
    KT_RUN(test_unpinned_follows_sender);

    // The remaining cases need to speak from a second local address. Where
    // the host offers none, they are skipped rather than failed: the two
    // cases above cover where a pinned session sends, which is what the fix
    // is for, and need one address only.
    if (host_other_available()) {
        KT_RUN(test_pinned_discards_other_host);
        KT_RUN(test_pinned_receives_after_stray);
        KT_RUN(test_pinned_recv_exact_skips_stray);
        KT_RUN(test_pinned_discard_bound);
        KT_RUN(test_set_remote_moves_pin);
    } else {
        printf("# SKIP the foreign-host cases: " HOST_OTHER " cannot be bound on this host\n");
    }

    return kt_summary();
}
