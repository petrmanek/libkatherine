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
#include <katherine/udp.h>

#include "ktest.h"

// Uncommon and high on purpose: this test claims no global resource the way
// the fixed 1555/1556 of a device session are one, so it needs no exclusive
// slot among the registered tests.
#define PORT_A            42630
#define PORT_B            42631

#define HOST_LOCAL        "127.0.0.1"

// The largest datagram a Gen2 readout was measured sending, on 2026-09-17.
// The queue has to hold at least one of these to be worth anything at all.
#define GEN2_DATAGRAM_MAX 64932

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

    KT_REQUIRE(katherine_udp_set_rcvbuf(&u, KATHERINE_UDP_RCVBUF_DEFAULT) == KATHERINE_E_OK);
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

    KT_REQUIRE(katherine_udp_set_rcvbuf(&u, KATHERINE_UDP_RCVBUF_DEFAULT) == KATHERINE_E_OK);
    KT_REQUIRE(katherine_udp_rcvbuf(&u, &at_default) == KATHERINE_E_OK);

    // Above any plausible ceiling, so this is the clamped case on every host.
    KT_CHECK_EQ(katherine_udp_set_rcvbuf(&u, (uint32_t) INT_MAX), KATHERINE_E_OK);
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

    KT_CHECK_EQ(katherine_udp_set_rcvbuf(&u, UINT32_MAX), KATHERINE_E_OK);

    uint32_t bytes = 0;
    KT_CHECK_EQ(katherine_udp_rcvbuf(&u, &bytes), KATHERINE_E_OK);
    KT_CHECK(bytes > 0);

    katherine_udp_fini(&u);
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_a_fresh_session_reports_its_queue);
    KT_RUN(test_reading_into_nothing_is_refused);
    KT_RUN(test_a_fresh_session_has_already_asked_for_the_default);
    KT_RUN(test_asking_larger_never_grants_less);
    KT_RUN(test_a_request_beyond_the_option_is_capped);

    return kt_summary();
}
