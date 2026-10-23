/**
 * \file
 * \brief Every finalizer, called twice.
 *
 * A finalizer that cannot be called twice is a trap in exactly the place
 * people reach for one: an error path that does not know how far setup got,
 * and a wrapper destructor that runs after an explicit close. Each of these
 * used to do real damage on the second call -- close a descriptor the process
 * had since reused, destroy a mutex twice, free a buffer twice, decrement
 * Winsock's startup count below zero.
 *
 * The descriptor case is the one with an observable that needs no sanitizer:
 * a zeroed session has sock == 0, which is stdin, so finalizing one used to
 * close it.
 *
 * \author Petr Mánek
 * \date 19.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <katherine/global.h>

#ifdef KATHERINE_WIN
#include <io.h>
#else
#include <fcntl.h>
#endif

#include <katherine/katherine.h>

#include "transport/udp.h"

#include "kports.h"
#include "ktest.h"

#define PORT_A (KPORTS_FINI + 0)
#define PORT_B (KPORTS_FINI + 1)

// A Windows socket is not a descriptor, so there a stray close cannot reach
// stdin and this only confirms the guard did not misfire.
static bool
stdin_is_open(void)
{
#ifdef KATHERINE_WIN
    return _get_osfhandle(0) != -1;
#else
    return fcntl(0, F_GETFD) != -1;
#endif
}

// ------------------------------------------------------------------

/* A session that was never opened holds sock == 0, and zero is stdin. */
static void
test_finalizing_an_unopened_session_leaves_stdin_alone(void)
{
    KT_REQUIRE(stdin_is_open());

    katherine_udp_t u;
    memset(&u, 0, sizeof(u));

    katherine_udp_fini(&u);
    KT_CHECK(stdin_is_open());

    katherine_udp_fini(&u);
    KT_CHECK(stdin_is_open());
}

/* And an opened one survives being finalized twice: the second call must not
 * close whatever the first descriptor number has since become. */
static void
test_a_session_can_be_finalized_twice(void)
{
    katherine_udp_t u;
    if (katherine_udp_init_bound(&u, "127.0.0.1", PORT_A, "127.0.0.1", PORT_B, 50) != KATHERINE_E_OK) {
        printf("# SKIP cannot bind 127.0.0.1:%d\n", PORT_A);
        return;
    }

    katherine_udp_fini(&u);

    /* Take the descriptor the session had, so a second close would hit this
     * one rather than nothing. */
    katherine_udp_t other;
    const bool took = katherine_udp_init_bound(&other, "127.0.0.1", PORT_B, "127.0.0.1", PORT_A, 50)
        == KATHERINE_E_OK;

    katherine_udp_fini(&u);

    if (took) {
        /* Still usable, which it would not be if the second fini had closed
         * it out from under us. */
        KT_CHECK_EQ(katherine_udp_send_exact(&other, "x", 1), KATHERINE_E_OK);
        katherine_udp_fini(&other);
    }
}

/* A transport's operation table is what says whether it holds anything. */
static void
test_a_transport_can_be_finalized_twice(void)
{
    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = PORT_A,
        .remote_address = "127.0.0.1",
        .remote_port    = PORT_B,
        .timeout_ms     = 50,
    };

    katherine_transport_t t;
    if (katherine_transport_init_udp(&t, &config) != KATHERINE_E_OK) {
        printf("# SKIP cannot bind 127.0.0.1:%d\n", PORT_A);
        return;
    }

    katherine_transport_fini(&t);
    KT_CHECK(t.ops == NULL);

    katherine_transport_fini(&t);

    /* And one that was never initialized at all. */
    katherine_transport_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    katherine_transport_fini(&zeroed);
}

/* An acquisition that never ran, and one finalized twice. */
static void
test_an_acquisition_can_be_finalized_twice(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    acq.device = &device;

    katherine_acquisition_fini(&acq);
    KT_CHECK(acq.device == NULL);

    katherine_acquisition_fini(&acq);
}

/* The whole device, which is what a caller who ignored a return code reaches
 * for next. */
static void
test_a_device_can_be_finalized_twice(void)
{
    KT_REQUIRE(stdin_is_open());

    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    katherine_device_fini(&device);
    KT_CHECK(stdin_is_open());

    katherine_device_fini(&device);
    KT_CHECK(stdin_is_open());
}

/* A finalizer called on an object whose initializer FAILED, which is the
 * other half of the same trap: an error path that does not know how far
 * setup got, and a wrapper destructor that runs anyway.
 *
 * Both of these used to crash, and the caller's object is poisoned rather
 * than zeroed here because that is what makes them crash -- a zeroed one
 * passes whether or not the initializer clears it.
 *
 * Python reached the device case: katherine.Device('x' * 300) segfaulted the
 * interpreter, because Cython runs __dealloc__ even when __cinit__ raised and
 * the block it frees came from PyMem_Malloc. */
static void
test_a_device_whose_init_failed_can_be_finalized(void)
{
    katherine_device_t device;
    memset(&device, 0xA5, sizeof(device));

    char too_long[KATHERINE_DEVICE_ADDRESS_STR_SIZE + 8];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';

    /* Refused at the first check, before any link is opened. */
    KT_CHECK_EQ(katherine_device_init(&device, too_long, 0), KATHERINE_E_ADDRESS);

    katherine_device_fini(&device);
}

static void
test_an_acquisition_whose_init_failed_can_be_finalized(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    katherine_acquisition_t acq;
    memset(&acq, 0xA5, sizeof(acq));

    /* The device carries no link, so opening the data one fails before
     * pixel_buffer is assigned -- which is what the finalizer frees. */
    KT_CHECK(katherine_acquisition_init(&acq, &device, NULL, 65536, 4096, 10, 10)
        != KATHERINE_E_OK);

    katherine_acquisition_fini(&acq);
}

int
main(void)
{
    KT_RUN(test_finalizing_an_unopened_session_leaves_stdin_alone);
    KT_RUN(test_a_session_can_be_finalized_twice);
    KT_RUN(test_a_transport_can_be_finalized_twice);
    KT_RUN(test_an_acquisition_can_be_finalized_twice);
    KT_RUN(test_a_device_can_be_finalized_twice);
    KT_RUN(test_a_device_whose_init_failed_can_be_finalized);
    KT_RUN(test_an_acquisition_whose_init_failed_can_be_finalized);
    return kt_summary();
}
