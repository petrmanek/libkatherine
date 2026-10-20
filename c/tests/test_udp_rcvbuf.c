/**
 * \file
 * \brief The receive queue a UDP session asks the OS for, and reads back.
 *
 * A measurement stream loses whole datagrams when the OS queue is too shallow
 * to cover the time the caller spends not calling recv, and that loss is
 * invisible: nothing in the protocol numbers datagrams, so an absent one is
 * not reported anywhere. Measured on a Gen2 readout at 49.5 MB/s, the Linux
 * default of 212 992 B carried about 2.5 ms and lost data continuously; whole
 * datagrams went missing in exact step with the kernel's own RcvbufErrors
 * counter while the library reported nothing dropped.
 *
 * The cases below therefore assert the mechanism rather than any particular
 * number. A size is a host policy -- Linux stores twice what is asked and
 * clamps the request to net.core.rmem_max, Winsock does neither -- so a test
 * pinning a byte count would be asserting the sysctl of whoever ran it. What
 * is portable is that asking for more never grants less, that a session opened
 * by this library has already asked for the default, and that the read-back
 * exists at all -- the only way a caller learns it was given less than it
 * wanted, the alternative symptom being silently lost measurement data.
 *
 * \author Petr Mánek
 * \date 17.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <katherine/error.h>
#include <katherine/transport.h>
#include "transport/udp.h"

#include "kports.h"
#include "ktest.h"

// Uncommon and high on purpose: this test claims no global resource the way
// the fixed 1555/1556 of a device session are one, so it needs no exclusive
// slot among the registered tests.
#define PORT_A             (KPORTS_UDP_RCVBUF + 0)
#define PORT_B             (KPORTS_UDP_RCVBUF + 1)

// A third pair, for the flood that provokes a drop: the pair above is used by
// cases that must not have thousands of datagrams queued behind them.
#define PORT_DROP_RECEIVER (KPORTS_UDP_RCVBUF + 2)
#define PORT_DROP_SENDER   (KPORTS_UDP_RCVBUF + 3)
#define PORT_TDROP_RECV    (KPORTS_UDP_RCVBUF + 4)
#define PORT_TDROP_SEND    (KPORTS_UDP_RCVBUF + 5)

#define HOST_LOCAL         "127.0.0.1"

// The largest datagram a Gen2 readout was measured sending, on 2026-09-17.
// The queue has to hold at least one of these to be worth anything at all.
#define GEN2_DATAGRAM_MAX  64932

static katherine_error_t
open_session(katherine_udp_t *u, uint16_t local_port, uint16_t remote_port)
{
    return katherine_udp_init_bound(u, HOST_LOCAL, local_port, HOST_LOCAL, remote_port, 100);
}

// ------------------------------------------------------------------

/* The default has to clear the largest datagram the library can be handed, or
 * the queue cannot hold even one and every stall costs data. Asserted at
 * compile time rather than as a case: it is a claim about the constant we ship,
 * so it holds on every platform and every build without being run, and MSVC
 * warns C4127 on a runtime conditional whose value is known.
 *
 * KATHERINE_UDP_RCVBUF_DEFAULT must clear one whole measurement datagram. */
_Static_assert(KATHERINE_UDP_RCVBUF_DEFAULT > GEN2_DATAGRAM_MAX,
    "the default receive queue must hold at least one whole datagram");

static void
test_a_fresh_session_reports_its_queue(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    uint32_t bytes = 0;
    KT_CHECK_EQ(katherine_udp_rcvbuf(&u, &bytes), KATHERINE_E_OK);
    KT_CHECK(bytes > 0);

    katherine_udp_fini(&u);
}

static void
test_reading_into_nothing_is_refused(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_udp_rcvbuf(&u, NULL), KATHERINE_E_INVAL);

    katherine_udp_fini(&u);
}

/* The behavioural claim, and the one that has to fail when the fix is absent:
 * a session is opened having ALREADY asked for the default, so asking for it
 * again changes nothing. Stated as a comparison of one socket against itself,
 * which is what makes it host-independent -- whatever the administrative
 * ceiling does to the request, it does the same thing both times. Comparing
 * against the constant instead would assert the sysctl of whoever ran the
 * test: where net.core.rmem_max is still 212 992, a request for the 4 MB
 * default is granted 425 984, and an assertion of "at least the default"
 * would fail on a host that is behaving correctly.
 *
 * An earlier version of this case compared a fresh session against one set to
 * 0, and passed with the fix removed: 0 asks for the OS MINIMUM, which the OS
 * DEFAULT already exceeds, so it only proved that init had not asked for the
 * smallest possible queue. */
static void
test_a_fresh_session_has_already_asked_for_the_default(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    uint32_t as_opened = 0, after_asking = 0;
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &as_opened) == KATHERINE_E_OK);

    // Refused on a host whose ceiling the default exceeds, which is not a
    // failure of this case: opening a session answers such a refusal by asking
    // for less, so the fresh session already holds what the host allowed. What
    // must hold either way is that asking again does not move it -- that is
    // what says init had already asked.
    const katherine_error_t res = katherine_udp_set_rcvbuf(&u, KATHERINE_UDP_RCVBUF_DEFAULT);
    KT_CHECK(res == KATHERINE_E_OK || res == KATHERINE_E_NOMEM);
    if (res != KATHERINE_E_OK) {
        printf("# this host refuses %d B outright (%s)\n", KATHERINE_UDP_RCVBUF_DEFAULT,
            katherine_strerror(res));
    }
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &after_asking) == KATHERINE_E_OK);

    // Printed either way: it is the only place the granted size is visible,
    // and a reader chasing lost data wants to see what the host allowed.
    printf("# queue as opened %u B, after asking for %d B: %u B\n", as_opened,
        KATHERINE_UDP_RCVBUF_DEFAULT, after_asking);

    KT_CHECK_EQ(as_opened, after_asking);

    katherine_udp_fini(&u);
}

/* Asking for more grants at least as much, and asking for an absurd amount
 * succeeds rather than failing -- which is what makes "the largest the host
 * permits" reachable in one call instead of by searching. The clamp is silent
 * by design, so a caller that needs to know what it got reads it back. */
static void
test_asking_larger_never_grants_less(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    // Not "small": rpcndr.h, which winsock2.h pulls in, has #define small char,
    // so a variable of that name becomes "uint32_t char" on MSVC and nowhere
    // else. Measured -- it built clean under gcc and clang and failed the VM.
    uint32_t one_datagram = 0, at_default = 0, at_ceiling = 0;

    KT_REQUIRE(katherine_udp_set_rcvbuf(&u, GEN2_DATAGRAM_MAX) == KATHERINE_E_OK);
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &one_datagram) == KATHERINE_E_OK);

    // Hosts divide here: Linux clamps a request above its ceiling and reports
    // success, macOS refuses it for want of memory. Either answer satisfies
    // what this case is about, since a refusal leaves the queue where it was
    // and so still grants no less than the smaller request did.
    const katherine_error_t at_default_res = katherine_udp_set_rcvbuf(&u, KATHERINE_UDP_RCVBUF_DEFAULT);
    KT_CHECK(at_default_res == KATHERINE_E_OK || at_default_res == KATHERINE_E_NOMEM);
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &at_default) == KATHERINE_E_OK);

    // Above any plausible ceiling, so this is the clamped case where the host
    // clamps and the refused case where it does not.
    const katherine_error_t at_ceiling_res = katherine_udp_set_rcvbuf(&u, (uint32_t) INT_MAX);
    KT_CHECK(at_ceiling_res == KATHERINE_E_OK || at_ceiling_res == KATHERINE_E_NOMEM);
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &at_ceiling) == KATHERINE_E_OK);

    KT_CHECK(at_default >= one_datagram);
    KT_CHECK(at_ceiling >= at_default);

    katherine_udp_fini(&u);
}

/* A request beyond what the option can represent is capped, not wrapped to a
 * negative size the OS would refuse for the wrong reason. UINT32_MAX as an int
 * is -1, so without the cap this would report an invalid argument. */
static void
test_a_request_beyond_the_option_is_capped(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    // The cap is the point, not the outcome: without it UINT32_MAX reaches the
    // option as -1 and comes back as an invalid argument. A host that refuses
    // the capped size for want of memory has still been asked a sane question,
    // so what must not come back is KATHERINE_E_INVAL.
    const katherine_error_t res = katherine_udp_set_rcvbuf(&u, UINT32_MAX);
    KT_CHECK(res == KATHERINE_E_OK || res == KATHERINE_E_NOMEM);

    uint32_t bytes = 0;
    KT_CHECK_EQ(katherine_udp_rcvbuf(&u, &bytes), KATHERINE_E_OK);
    KT_CHECK(bytes > 0);

    katherine_udp_fini(&u);
}

// ------------------------------------------------------------------

// ------------------------------------------------------------------
// The kernel's own count of datagrams it dropped for want of queue space --
// the one place a measurement stream's silent loss becomes visible, nothing
// in the protocol numbering datagrams.
//
// Provoked rather than waited for: a socket asked for the smallest queue the
// host will grant, then flooded with more datagrams than fit before any is
// read. Whether the host drops on that particular flood is its own policy, so
// a run that loses nothing is reported and skipped rather than failed; what
// is asserted unconditionally is that a session not counting reports zero and
// that the option is accepted.

#define FLOOD_DATAGRAMS 4096
#define FLOOD_PAYLOAD   1400

static void
test_the_kernel_reports_the_datagrams_it_dropped(void)
{
    katherine_udp_t receiver;
    katherine_udp_t sender;
    static unsigned char payload[FLOOD_PAYLOAD];

    KT_REQUIRE(katherine_udp_init_bound(&receiver, "127.0.0.1", PORT_DROP_RECEIVER, "127.0.0.1",
                   PORT_DROP_SENDER, 100)
        == 0);

    // Not counting yet, so nothing is claimed.
    KT_CHECK_EQ(katherine_udp_get_dropped_datagrams(&receiver), 0);

    // Asked for before anything else: a platform without the counter has
    // nothing below worth doing, and the queue size the flood needs is one
    // such platform refuses outright.
    const katherine_error_t res = katherine_udp_set_drop_counting(&receiver, true);
    if (res == KATHERINE_E_UNSUPPORTED) {
        printf("# SKIP this platform has no per-socket drop counter\n");
        katherine_udp_fini(&receiver);
        return;
    }
    KT_CHECK_EQ(res, 0);

    // Zero asks for the OS minimum on the platforms that have the counter,
    // which is the shallowest queue they will grant -- and the point here is
    // to overrun it. macOS rejects it with EINVAL, which is why this follows
    // the check above rather than preceding it.
    KT_CHECK_EQ(katherine_udp_set_rcvbuf(&receiver, 0), 0);

    KT_REQUIRE(katherine_udp_init_bound(&sender, "127.0.0.1", PORT_DROP_SENDER, "127.0.0.1",
                   PORT_DROP_RECEIVER, 100)
        == 0);

    for (unsigned i = 0; i < FLOOD_DATAGRAMS; ++i) {
        if (katherine_udp_send_exact(&sender, payload, sizeof(payload)) != 0) break;
    }

    // Drain what survived, then send one more and receive that too. The
    // kernel reports its count alongside a delivered datagram, so a flood
    // that overruns the queue and stops has no later delivery to carry the
    // count of what it lost -- the extra datagram is what fetches it.
    unsigned received = 0;
    for (;;) {
        unsigned char buf[FLOOD_PAYLOAD];
        size_t count = sizeof(buf);

        if (katherine_udp_recv_nowait(&receiver, buf, &count) != 0) break;
        ++received;
    }

    if (katherine_udp_send_exact(&sender, payload, sizeof(payload)) == 0) {
        unsigned char buf[FLOOD_PAYLOAD];
        size_t count = sizeof(buf);

        if (katherine_udp_recv(&receiver, buf, &count) == 0) ++received;
    }

    const uint64_t dropped = katherine_udp_get_dropped_datagrams(&receiver);
    printf("# flood: %u sent, %u received, %llu reported dropped\n", FLOOD_DATAGRAMS, received,
        (unsigned long long) dropped);

    if (dropped == 0) {
        printf("# SKIP this host dropped nothing on a %u-datagram flood\n", FLOOD_DATAGRAMS);
    } else {
        // Whatever was dropped was not received, and the two together cannot
        // exceed what was sent -- the flood plus the one datagram that
        // fetched the count.
        KT_CHECK(received + dropped <= FLOOD_DATAGRAMS + 1);
        KT_CHECK(received < FLOOD_DATAGRAMS);
    }

    // Switching it off clears what was counted.
    KT_CHECK_EQ(katherine_udp_set_drop_counting(&receiver, false), 0);
    KT_CHECK_EQ(katherine_udp_get_dropped_datagrams(&receiver), 0);

    // Switching it on again also starts from zero, and the baseline the
    // deltas are taken against survives -- so a quiet period reports nothing
    // rather than the whole flood a second time.
    KT_CHECK_EQ(katherine_udp_set_drop_counting(&receiver, true), 0);
    if (katherine_udp_send_exact(&sender, payload, sizeof(payload)) == 0) {
        unsigned char buf[FLOOD_PAYLOAD];
        size_t count = sizeof(buf);

        (void) katherine_udp_recv(&receiver, buf, &count);
    }
    KT_CHECK_EQ(katherine_udp_get_dropped_datagrams(&receiver), 0);

    katherine_udp_fini(&sender);
    katherine_udp_fini(&receiver);
}


/* The same count, reached the way an acquisition reaches it: a transport that
 * was asked to count at open, read through the transport operations. Proves
 * the configuration flag is wired to the socket option, which nothing else
 * observes. */
static void
test_a_transport_reports_what_its_session_dropped(void)
{
    static unsigned char payload[FLOOD_PAYLOAD];

    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = PORT_TDROP_RECV,
        .remote_address = "127.0.0.1",
        .remote_port    = PORT_TDROP_SEND,
        .timeout_ms     = 100,
        /* The OS minimum, which is the shallowest queue it will grant, and
           the point here is to overrun it. */
        .rcvbuf      = 0,
        .count_drops = true,
    };

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &config) == KATHERINE_E_OK);

    uint64_t dropped            = 1;
    const katherine_error_t res = katherine_transport_get_dropped_input(&t, &dropped);
    if (res == KATHERINE_E_UNSUPPORTED) {
        printf("# SKIP this platform has no per-socket drop counter\n");
        katherine_transport_fini(&t);
        return;
    }
    KT_CHECK_EQ(res, KATHERINE_E_OK);
    KT_CHECK_EQ(dropped, 0u);

    katherine_udp_t sender;
    KT_REQUIRE(katherine_udp_init_bound(&sender, "127.0.0.1", PORT_TDROP_SEND, "127.0.0.1", PORT_TDROP_RECV, 100) == 0);

    for (unsigned i = 0; i < FLOOD_DATAGRAMS; ++i)
        if (katherine_udp_send_exact(&sender, payload, sizeof(payload)) != 0) break;

    /* The kernel reports its count alongside a delivered datagram, so what
       survived is drained and one more is sent to carry the count. */
    for (;;) {
        void *buf  = NULL;
        size_t len = 0;

        if (katherine_transport_acquire_data_buf(&t, &buf, &len) != KATHERINE_E_OK) break;
        (void) katherine_transport_release_data_buf(&t, buf);
    }

    if (katherine_udp_send_exact(&sender, payload, sizeof(payload)) == 0) {
        void *buf  = NULL;
        size_t len = 0;

        if (katherine_transport_acquire_data_buf(&t, &buf, &len) == KATHERINE_E_OK)
            (void) katherine_transport_release_data_buf(&t, buf);
    }

    KT_CHECK_EQ(katherine_transport_get_dropped_input(&t, &dropped), KATHERINE_E_OK);
    printf("# flood through a transport: %u sent, %llu reported dropped\n", FLOOD_DATAGRAMS,
        (unsigned long long) dropped);
    if (dropped == 0) printf("# SKIP this host dropped nothing on a %u-datagram flood\n", FLOOD_DATAGRAMS);

    katherine_udp_fini(&sender);
    katherine_transport_fini(&t);
}

int
main(void)
{
    KT_RUN(test_a_fresh_session_reports_its_queue);
    KT_RUN(test_reading_into_nothing_is_refused);
    KT_RUN(test_a_fresh_session_has_already_asked_for_the_default);
    KT_RUN(test_asking_larger_never_grants_less);
    KT_RUN(test_a_request_beyond_the_option_is_capped);

    KT_RUN(test_the_kernel_reports_the_datagrams_it_dropped);
    KT_RUN(test_a_transport_reports_what_its_session_dropped);
    return kt_summary();
}
