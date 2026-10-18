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
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/transport.h>
#include <katherine/udp.h>

#include "kports.h"
#include "ktest.h"

#define PORT_A     (KPORTS_TRANSPORT + 0)
#define PORT_B     (KPORTS_TRANSPORT + 1)
#define HOST_LOCAL "127.0.0.1"
#define BUF_BYTES  2048

static katherine_error_t
open_session(katherine_udp_t *u, uint16_t local_port, uint16_t remote_port)
{
    return katherine_udp_init_bound(u, HOST_LOCAL, local_port, HOST_LOCAL, remote_port, 100);
}

// ------------------------------------------------------------------

/* A session driven through the operations carries what it carried before. */
static void
test_a_lent_buffer_holds_what_arrived(void)
{
    katherine_udp_t sender, receiver;
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

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
    katherine_udp_fini(&receiver);
    katherine_udp_fini(&sender);
}

/* Giving back a buffer the transport never lent is a caller error, not a
 * silent no-op: on a transport with a ring of them it would resubmit a
 * transfer that is already outstanding. */
static void
test_returning_a_foreign_buffer_is_refused(void)
{
    katherine_udp_t receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    char elsewhere[8];
    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, elsewhere), KATHERINE_E_INVAL);

    katherine_transport_fini(&t);
    katherine_udp_fini(&receiver);
}

/* An empty buffer can hold no datagram, and a transport over no session has
 * nothing to read. */
static void
test_a_transport_over_nothing_is_refused(void)
{
    katherine_udp_t u;
    KT_REQUIRE(open_session(&u, PORT_A, PORT_B) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_CHECK_EQ(katherine_transport_init_udp(&t, NULL), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_transport_init_udp(NULL, &u), KATHERINE_E_INVAL);

    katherine_udp_fini(&u);
}

/* Silence is a timeout, not a zero-length buffer: a caller told it was lent
 * nothing would decode whatever the buffer held last. */
static void
test_silence_times_out(void)
{
    katherine_udp_t receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&t);
    katherine_udp_fini(&receiver);
}

/* The kind travels with the transport, so a caller can tell what it got. */
static void
test_a_udp_transport_says_so(void)
{
    katherine_udp_t receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);
    KT_CHECK_EQ(t.kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK(t.ops != NULL);
    KT_CHECK(t.ctx != NULL);

    katherine_transport_fini(&t);
    katherine_udp_fini(&receiver);
}

/* Resizing is exact, so a caller that sized something else to match still
 * knows the capacity afterwards -- which is what lets an acquisition keep its
 * own buffer size as the truth about the buffer it reads from. */
static void
test_a_transport_resizes_exactly(void)
{
    katherine_udp_t sender, receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    /* Grown, then shrunk: a datagram filling the new size comes back whole at
     * each, which is the capacity the caller was promised. */
    const size_t sizes[2] = {BUF_BYTES * 4, BUF_BYTES / 2};
    for (size_t i = 0; i < 2; ++i) {
        KT_CHECK_EQ(katherine_transport_set_buffer_size(&t, sizes[i]), KATHERINE_E_OK);

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
    }

    KT_CHECK_EQ(katherine_transport_set_buffer_size(&t, 0), KATHERINE_E_INVAL);

    katherine_transport_fini(&t);
    katherine_udp_fini(&sender);
    katherine_udp_fini(&receiver);
}

/* Resizing under a caller still holding the buffer would hand them a pointer
 * into freed memory, so it is refused rather than done. */
static void
test_resizing_while_lent_is_refused(void)
{
    katherine_udp_t sender, receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);
    KT_REQUIRE(open_session(&sender, PORT_A, PORT_B) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    const uint8_t payload[4] = {1, 2, 3, 4};
    KT_REQUIRE(katherine_udp_send_exact(&sender, payload, sizeof(payload)) == KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_REQUIRE(katherine_transport_acquire_data_buf(&t, &buf, &len) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_transport_set_buffer_size(&t, BUF_BYTES * 2), KATHERINE_E_STATE);

    /* Given back, it resizes. */
    KT_REQUIRE(katherine_transport_release_data_buf(&t, buf) == KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_transport_set_buffer_size(&t, BUF_BYTES * 2), KATHERINE_E_OK);

    katherine_transport_fini(&t);
    katherine_udp_fini(&sender);
    katherine_udp_fini(&receiver);
}

/* The lock round-trips through the transport. That it is the session's own
 * lock rather than one the transport keeps beside it is by construction --
 * the UDP backend delegates -- and matters because the command layer still
 * takes that session's lock directly. */
static void
test_the_lock_round_trips(void)
{
    katherine_udp_t receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_transport_mutex_lock(&t), KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_transport_mutex_unlock(&t), KATHERINE_E_OK);

    /* Taken and released again, so the first release really released it. */
    KT_CHECK_EQ(katherine_transport_mutex_lock(&t), KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_transport_mutex_unlock(&t), KATHERINE_E_OK);

    katherine_transport_fini(&t);
    katherine_udp_fini(&receiver);
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
    katherine_udp_t receiver;
    KT_REQUIRE(open_session(&receiver, PORT_B, PORT_A) == KATHERINE_E_OK);

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &receiver) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_transport_last_os_error(&t), 0);

    const size_t oversized = 200000;
    char *payload          = (char *) calloc(1, oversized);
    KT_REQUIRE(payload != NULL);
    KT_CHECK(katherine_transport_send_cmd(&t, payload, oversized) != KATHERINE_E_OK);
    free(payload);

    const int err = katherine_transport_last_os_error(&t);
    KT_CHECK(err != 0);
    KT_CHECK_EQ(err, katherine_udp_last_os_error(&receiver));

    katherine_transport_fini(&t);
    katherine_udp_fini(&receiver);
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_a_lent_buffer_holds_what_arrived);
    KT_RUN(test_returning_a_foreign_buffer_is_refused);
    KT_RUN(test_a_transport_over_nothing_is_refused);
    KT_RUN(test_silence_times_out);
    KT_RUN(test_a_udp_transport_says_so);
    KT_RUN(test_a_transport_resizes_exactly);
    KT_RUN(test_resizing_while_lent_is_refused);
    KT_RUN(test_the_lock_round_trips);
    KT_RUN(test_the_os_error_comes_through);


    return kt_summary();
}
