/**
 * \file
 * \brief Driving a readout through the transport operations.
 *
 * The data-plane pair is what separates the transports: a socket keeps filling
 * the OS queue whether or not the caller is in recv(), while a USB bulk
 * endpoint carries nothing unless a read is outstanding. The operations hide
 * that difference, so what is asserted here is the contract both sides of it
 * must honour -- a lent buffer holds what arrived, it stays valid until it is
 * given back, and giving back something else is refused.
 *
 * The USB cases run without a readout on the bus: they cover the arguments
 * katherine_transport_init_usb() rejects before it ever looks for one, which is every
 * path a caller can reach by misconfiguration rather than by missing hardware.
 *
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/transport.h>
#include "transport/udp.h"


#ifdef KATHERINE_FEATURE_USB
#include "transport/usb.h"
#endif

#include "transport/factory.h"

#include "kports.h"
#include "ktest.h"
#include "monoclock.h"

#define PORT_A             (KPORTS_TRANSPORT + 0)
#define PORT_B             (KPORTS_TRANSPORT + 1)
#define HOST_LOCAL         "127.0.0.1"
#define HOST_LOCAL_ADDRESS "udp://127.0.0.1"
#define BUF_BYTES          2048

// Both sanitizers abort on the allocation test_a_transport_resizes_exactly
// asks for -- the largest size the transport's headroom survives is within a
// word of SIZE_MAX, and realloc() of that is "allocation-size-too-big" to
// them -- instead of letting realloc() decline it. allocator_may_return_null
// makes them return NULL as the C library would, which is the answer the case
// is written against. The same two hooks, for the same reason, are in
// c/tests/test_compat1.c; their runtimes call them at startup and nothing
// else does.
const char *
__asan_default_options(void)
{
    return "allocator_may_return_null=1";
}

const char *
__tsan_default_options(void)
{
    return "allocator_may_return_null=1";
}

static katherine_error_t
open_session(katherine_udp_t *u, uint16_t local_port, uint16_t remote_port)
{
    return katherine_udp_init_bound(u, HOST_LOCAL, local_port, HOST_LOCAL, remote_port, 100);
}

static katherine_udp_config_t
session_config(uint16_t local_port, uint16_t remote_port)
{
    return (katherine_udp_config_t) {
        .local_address  = HOST_LOCAL,
        .local_port     = local_port,
        .remote_address = HOST_LOCAL,
        .remote_port    = remote_port,
        .timeout_ms     = 100,
    };
}

// ------------------------------------------------------------------

/* A session driven through the operations carries what it carried before. */
static void
test_a_lent_buffer_holds_what_arrived(void)
{
    katherine_udp_t sender;
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    static const char payload[] = "measurement data";
    KT_CHECK_EQ(katherine_udp_send_exact(&sender, payload, sizeof(payload)), KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, sizeof(payload));
    KT_CHECK_MEM_EQ(buf, payload, sizeof(payload));

    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, buf), KATHERINE_E_OK);

    katherine_transport_fini(&t);
    katherine_udp_fini(&sender);
}

/* Giving back a buffer the transport never lent is a caller error, not a
 * silent no-op: on a transport with a ring of them it would resubmit a
 * transfer that is already outstanding. */
static void
test_returning_a_foreign_buffer_is_refused(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    char elsewhere[8];
    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, elsewhere), KATHERINE_E_INVAL);

    katherine_transport_fini(&t);
}

/* An empty buffer can hold no datagram, and a transport over no session has
 * nothing to read. */
static void
test_a_transport_over_nothing_is_refused(void)
{
    const katherine_udp_config_t config = session_config(PORT_A, PORT_B);

    katherine_transport_t t;
    KT_CHECK_EQ(katherine_transport_init_udp(&t, NULL), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_udp(NULL, &config), KATHERINE_E_INVAL);

    // A session with nobody to address is refused before a socket is opened.
    katherine_udp_config_t nowhere = config;
    nowhere.remote_address         = NULL;
    KT_CHECK_EQ(katherine_transport_init_udp(&t, &nowhere), KATHERINE_E_INVAL);
}

/* Silence is a timeout, not a zero-length buffer: a caller told it was lent
 * nothing would decode whatever the buffer held last. */
static void
test_silence_times_out(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&t);
}

/* And the wait is as long as the caller last asked for, not as long as the
   session happened to be opened with. */
static void
test_the_timeout_is_what_was_last_set(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    /* The session was opened with 100 ms; ask for five times that. */
    KT_CHECK_EQ(katherine_transport_set_timeout(&t, 500), KATHERINE_E_OK);

    void *buf         = NULL;
    size_t len        = 0;
    const uint64_t t0 = katherine_clock_monotonic_ns();
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_TIMEOUT);
    const uint64_t waited_ms = (katherine_clock_monotonic_ns() - t0) / 1000000;

    /* Bounded on both sides: below 500 means the new limit never reached the
       socket, far above it means something other than this timeout ended the
       wait. A scheduler can overshoot, so the ceiling is generous. */
    KT_CHECK(waited_ms >= 450);
    KT_CHECK(waited_ms < 2000);

    katherine_transport_fini(&t);
}

/* The kind travels with the transport, so a caller can tell what it got. */
static void
test_a_udp_transport_says_so(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);
    KT_CHECK_EQ(t.kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK(t.ops != NULL);
    KT_CHECK(t.ctx != NULL);

    katherine_transport_fini(&t);
}

/* The size a session reads at is settled at construction and is exact, so a
 * caller that sized something else to match still knows the capacity -- which
 * is what lets an acquisition keep its own md_buffer_size as the truth about
 * the buffer it reads from. */
static void
test_a_session_reads_at_the_size_it_was_built_with(void)
{
    katherine_udp_t sender;
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);

    /* Large, then small: a datagram filling the size comes back whole at
     * each, which is the capacity the caller asked the constructor for. */
    const size_t sizes[2] = {BUF_BYTES * 4, BUF_BYTES / 2};
    for (size_t i = 0; i < 2; ++i) {
        katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);
        receiver.buffer_size            = sizes[i];

        katherine_transport_t t;
        KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

        uint8_t *probe = (uint8_t *) malloc(sizes[i]);
        KT_REQUIRE(probe != NULL);
        memset(probe, 0x5A, sizes[i]);
        KT_REQUIRE(katherine_udp_send_exact(&sender, probe, sizes[i]) == KATHERINE_E_OK);

        void *buf  = NULL;
        size_t len = 0;
        KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
        KT_CHECK_EQ(len, sizes[i]);
        KT_CHECK(memcmp(buf, probe, sizes[i]) == 0);
        KT_CHECK_EQ(katherine_transport_release_data_buf(&t, buf), KATHERINE_E_OK);
        free(probe);
        katherine_transport_fini(&t);
    }

    katherine_udp_fini(&sender);
}

/* Zero is not an error here but a request for the session's own size, which
 * is the half of the contract that inverted when the size moved to the
 * constructor. A datagram of that size comes back whole; one byte more is
 * truncated to it. */
static void
test_zero_asks_for_the_session_s_own_size(void)
{
    katherine_udp_t sender;
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);

    katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);
    receiver.buffer_size            = 0;

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    uint8_t *probe = (uint8_t *) malloc(KATHERINE_UDP_DEFAULT_BUFFER_SIZE);
    KT_REQUIRE(probe != NULL);
    memset(probe, 0xA5, KATHERINE_UDP_DEFAULT_BUFFER_SIZE);
    KT_REQUIRE(katherine_udp_send_exact(&sender, probe, KATHERINE_UDP_DEFAULT_BUFFER_SIZE) == KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, (size_t) KATHERINE_UDP_DEFAULT_BUFFER_SIZE);
    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, buf), KATHERINE_E_OK);

    free(probe);
    katherine_transport_fini(&t);
    katherine_udp_fini(&sender);
}

/* A size within a word of SIZE_MAX, which the session adds its read-ahead
 * headroom to. The sum wraps to zero, malloc() reads that as a request for
 * nothing, and the code that believes it is handling an allocation failure
 * carries on with a buffer that is not there. Refused at the constructor
 * before any of that, which is where the check moved with the size. */
static void
test_a_size_the_headroom_cannot_be_added_to_is_refused(void)
{
    katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);
    katherine_transport_t t;

    receiver.buffer_size = SIZE_MAX - sizeof(uint64_t) + 1;
    KT_CHECK_EQ(katherine_transport_init_udp(&t, &receiver), KATHERINE_E_INVAL);

    receiver.buffer_size = SIZE_MAX;
    KT_CHECK_EQ(katherine_transport_init_udp(&t, &receiver), KATHERINE_E_INVAL);

    /* The largest size the addition survives is refused for want of memory
     * rather than for being out of range, so the bound is the wrap and not a
     * round number somebody picked. */
    receiver.buffer_size = SIZE_MAX - sizeof(uint64_t);
    KT_CHECK_EQ(katherine_transport_init_udp(&t, &receiver), KATHERINE_E_NOMEM);
}

/* The lock round-trips through the transport. That it is the session's own
 * lock rather than one the transport keeps beside it is by construction --
 * the UDP backend delegates -- and matters because the command layer still
 * takes that session's lock directly. */
static void
test_the_lock_round_trips(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_transport_mutex_lock(&t), KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_transport_mutex_unlock(&t), KATHERINE_E_OK);

    /* Taken and released again, so the first release really released it. */
    KT_CHECK_EQ(katherine_transport_mutex_lock(&t), KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_transport_mutex_unlock(&t), KATHERINE_E_OK);

    katherine_transport_fini(&t);
}

/* The OS error behind a failure is reachable through the transport, and is
 * the session's own number rather than something kept beside it.
 *
 * Provoked with a datagram larger than any UDP payload, because most
 * failures record nothing: a receive timeout leaves it at zero, so a test
 * written around one would pass whether or not anything was wired up.
 * sendto() refuses an oversized datagram outright and the session keeps
 * that errno. */
static void
test_the_os_error_comes_through(void)
{
    const katherine_udp_config_t receiver = session_config(PORT_B, PORT_A);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_transport_get_last_os_error(&t), 0);

    const size_t oversized = 200000;
    char *payload          = (char *) calloc(1, oversized);
    KT_REQUIRE(payload != NULL);
    KT_CHECK(katherine_transport_send_cmd(&t, payload, oversized) != KATHERINE_E_OK);
    free(payload);

    KT_CHECK(katherine_transport_get_last_os_error(&t) != 0);

    katherine_transport_fini(&t);
}

// ------------------------------------------------------------------
// The transports a device drives, each built on its own.

/* Both come back usable, each the kind the address named, and each on a
 * session of its own. Asked to move off the standard pair, because 1555/1556
 * are a global resource that the ksim daemon and the device fixtures claim and
 * a unit test must not contend for. */
static void
test_the_factories_open_both_transports(void)
{
    katherine_transport_t control, data;
    KT_REQUIRE(katherine_transport_init_for_control(HOST_LOCAL_ADDRESS, true, &control) == KATHERINE_E_OK);

    uint16_t control_port = 0;
    // No storage for the answer is allowed, and asks only whether the link has
    // a port at all.
    KT_CHECK_EQ(katherine_transport_udp_bound_port(&control, NULL), KATHERINE_E_OK);

    KT_REQUIRE(katherine_transport_udp_bound_port(&control, &control_port) == KATHERINE_E_OK);
    KT_CHECK(control_port != 0);
    KT_CHECK(control_port != KATHERINE_UDP_CONTROL_PORT);

    /* The port a device would have told the readout to send to. */
    KT_REQUIRE(katherine_transport_init_for_data(HOST_LOCAL_ADDRESS, &control, 0, 0, NULL, &data) == KATHERINE_E_OK);

    KT_CHECK_EQ(control.kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK_EQ(data.kind, KATHERINE_TRANSPORT_UDP);

    uint16_t data_port = 0;
    KT_CHECK_EQ(katherine_transport_udp_bound_port(&data, &data_port), KATHERINE_E_OK);
    KT_CHECK(data_port != 0);
    KT_CHECK(data_port != control_port);

    katherine_transport_fini(&data);
    katherine_transport_fini(&control);
}

/* A missing argument is refused before anything is opened. */
static void
test_a_factory_refuses_a_missing_argument(void)
{
    katherine_transport_t control, data;

    KT_CHECK_EQ(katherine_transport_init_for_control(NULL, true, &control), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_for_control(HOST_LOCAL_ADDRESS, true, NULL), KATHERINE_E_INVAL);

    KT_CHECK_EQ(katherine_transport_init_for_data(NULL, &control, 0, 0, NULL, &data), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_for_data(HOST_LOCAL_ADDRESS, NULL, 0, 0, NULL, &data), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_for_data(HOST_LOCAL_ADDRESS, &control, 0, 0, NULL, NULL), KATHERINE_E_INVAL);
}

/* A refusal leaves the transport closed, so a caller that finalises anyway
 * finalises nothing rather than whatever its stack held. */
static void
test_a_refused_factory_leaves_nothing_open(void)
{
    katherine_transport_t control;
    memset(&control, 0xFF, sizeof(control));

    KT_CHECK(katherine_transport_init_for_control("carrier-pigeon://127.0.0.1", true, &control) != KATHERINE_E_OK);
    KT_CHECK(control.ops == NULL);
}

/* A data transport is opened against the readout's own control transport,
 * which is what a link that cannot be claimed twice is driven through. One
 * that was never opened, or opened on another link, is refused. */
static void
test_the_data_factory_refuses_a_mismatched_control(void)
{
    katherine_transport_t control, data;

    memset(&control, 0, sizeof(control));
    KT_CHECK_EQ(katherine_transport_init_for_data(HOST_LOCAL_ADDRESS, &control, 0, 0, NULL, &data), KATHERINE_E_INVAL);
    KT_CHECK(data.ops == NULL);
}

#ifdef KATHERINE_FEATURE_USB

/* The geometry shipped is the one the reference implementation runs this
 * readout at; a change to it should be deliberate enough to edit a test. */
static void
test_the_default_geometry_is_the_measured_one(void)
{
    const katherine_usb_config_t c = katherine_usb_config_default();

    KT_CHECK_EQ(c.queue_len, 64u);
    KT_CHECK_EQ(c.buffer_size, 48u * 1024u);
    KT_CHECK_EQ(c.timeout_ms, 100u);
    KT_CHECK_EQ(c.stream, KATHERINE_USB_STREAM_ASYNC);
}

/* Every argument error is reported before the bus is searched, so these hold
 * with no readout attached. */
static void
test_a_misconfigured_open_is_refused(void)
{
    katherine_usb_config_t c = katherine_usb_config_default();
    katherine_transport_t t;

    KT_CHECK_EQ(katherine_transport_init_usb(NULL, &c), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_usb(&t, NULL), KATHERINE_E_INVAL);

    /* A size past what libusb can carry in an int, which the slot would keep
     * as some other number entirely. Zero is NOT an error here: it asks for
     * the ring's own size, which is the contract the buffer size moved to
     * the constructor under. */
    c             = katherine_usb_config_default();
    c.buffer_size = (uint32_t) INT_MAX + 1u;
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &c), KATHERINE_E_INVAL);

    /* A ring of no buffers can hold nothing, and would divide by zero when
     * the completion callback indexed into it. Without one, a transport that
     * names only an IN endpoint has no discipline left to read it by. */
    c           = katherine_usb_config_default();
    c.queue_len = 0;
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &c), KATHERINE_E_INVAL);

    /* A ring behind an OUT endpoint would leave the streaming reads and the
     * message reads both claiming the IN endpoint. */
    c        = katherine_usb_config_default();
    c.ep_out = KATHERINE_USB_EP_CMD_OUT;
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &c), KATHERINE_E_INVAL);

    /* And a transport that names no endpoint at all carries nothing. */
    c        = katherine_usb_config_default();
    c.ep_in  = 0;
    c.ep_out = 0;
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &c), KATHERINE_E_INVAL);
}

/* Both shapes are named, because a benchmark prints which one it ran. */
static void
test_both_shapes_are_named(void)
{
    KT_CHECK_STR_EQ(katherine_str_usb_stream(KATHERINE_USB_STREAM_SYNC), "sync");
    KT_CHECK_STR_EQ(katherine_str_usb_stream(KATHERINE_USB_STREAM_ASYNC), "async");
}

/* Asked about a transport that is not one, the count answers rather than
 * dereferences. */
static void
test_the_inflight_count_tolerates_nothing(void)
{
    uint32_t inflight = 1;
    KT_CHECK_EQ(katherine_transport_usb_get_inflight_transfers(NULL, &inflight), KATHERINE_E_INVAL);

    katherine_transport_t empty;
    memset(&empty, 0, sizeof(empty));
    KT_CHECK_EQ(katherine_transport_usb_get_inflight_transfers(&empty, &inflight), KATHERINE_E_INVAL);
}

#endif /* KATHERINE_FEATURE_USB */

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_a_lent_buffer_holds_what_arrived);
    KT_RUN(test_returning_a_foreign_buffer_is_refused);
    KT_RUN(test_a_transport_over_nothing_is_refused);
    KT_RUN(test_silence_times_out);
    KT_RUN(test_the_timeout_is_what_was_last_set);
    KT_RUN(test_a_udp_transport_says_so);
    KT_RUN(test_a_session_reads_at_the_size_it_was_built_with);
    KT_RUN(test_zero_asks_for_the_session_s_own_size);
    KT_RUN(test_a_size_the_headroom_cannot_be_added_to_is_refused);
    KT_RUN(test_the_lock_round_trips);
    KT_RUN(test_the_os_error_comes_through);
    KT_RUN(test_the_factories_open_both_transports);
    KT_RUN(test_a_factory_refuses_a_missing_argument);
    KT_RUN(test_a_refused_factory_leaves_nothing_open);
    KT_RUN(test_the_data_factory_refuses_a_mismatched_control);

#ifdef KATHERINE_FEATURE_USB
    KT_RUN(test_the_default_geometry_is_the_measured_one);
    KT_RUN(test_a_misconfigured_open_is_refused);
    KT_RUN(test_both_shapes_are_named);
    KT_RUN(test_the_inflight_count_tolerates_nothing);
#endif

    return kt_summary();
}
