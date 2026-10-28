/**
 * \file
 * \brief Two threads on one device's control plane.
 *
 * The thread-safety contract in katherine.h says control-plane calls are
 * serialized by the link's own mutex, and that the serialization covers a
 * whole exchange rather than each send. That is the half worth pinning: a
 * lock released between the send and the receive would let one thread be
 * handed the other's answer, which correlation rejects as a stray in the
 * lucky case and accepts in the unlucky one.
 *
 * The readout here is a stub that answers with the operation code it last
 * saw and sleeps between the two, which widens the window a real link
 * leaves. With the stub's mutex in place every exchange gets its own
 * answer; with it removed -- the negative control run while this was
 * written -- about 1740 of 2000 per thread come back wrong, so the check
 * is nowhere near vacuous.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>

#include "msleep.h"
#include "mutex.h"
#include "protocol/cmd_interface.h"
#include "transport/ops.h"

#include "kthread.h"
#include "ktest.h"

#define EXCHANGES 2000

static katherine_mutex_t g_link_mutex;
static uint8_t g_last_opcode;
static katherine_device_t g_device;
static int g_mismatch[2];

static katherine_error_t
stub_lock(void *ctx)
{
    (void) ctx;
    return katherine_mutex_lock(&g_link_mutex);
}

static katherine_error_t
stub_unlock(void *ctx)
{
    (void) ctx;
    return katherine_mutex_unlock(&g_link_mutex);
}

static katherine_error_t
stub_flush(void *ctx, uint32_t *discarded)
{
    (void) ctx;
    if (discarded != NULL) *discarded = 0;
    return KATHERINE_E_OK;
}

// Records the command and then yields, which is the window a lock that
// did not span the exchange would leave open.
static katherine_error_t
stub_send(void *ctx, const void *data, size_t count)
{
    (void) ctx;
    if (count >= 8) g_last_opcode = ((const uint8_t *) data)[6];
    katherine_msleep(0);
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_recv(void *ctx, void *crd, size_t *len)
{
    (void) ctx;
    memset(crd, 0, KATHERINE_CMD_CRD_SIZE);
    ((uint8_t *) crd)[6] = g_last_opcode;
    if (len != NULL) *len = KATHERINE_CMD_CRD_SIZE;
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_timeout(void *ctx, uint32_t timeout_ms)
{
    (void) ctx;
    (void) timeout_ms;
    return KATHERINE_E_OK;
}

static int
stub_os_error(void *ctx)
{
    (void) ctx;
    return 0;
}

static const katherine_transport_ops_t STUB_OPS = {
    .send_cmd          = stub_send,
    .recv_crd          = stub_recv,
    .flush_incoming    = stub_flush,
    .set_timeout       = stub_timeout,
    .mutex_lock        = stub_lock,
    .mutex_unlock      = stub_unlock,
    .get_last_os_error = stub_os_error,
};

// Two different commands, so an answer meant for the other thread decodes
// as the wrong one and the call fails.
static void *
worker(void *arg)
{
    const int which = *(const int *) arg;

    for (int i = 0; i < EXCHANGES; ++i) {
        katherine_error_t res;

        if (which == 0) {
            katherine_readout_status_t status;
            res = katherine_get_readout_status(&g_device, &status);
        } else {
            katherine_comm_status_t comm;
            res = katherine_get_comm_status(&g_device, &comm);
        }

        if (res != KATHERINE_E_OK) ++g_mismatch[which];
    }

    return NULL;
}

// ------------------------------------------------------------------

static void
test_an_exchange_is_not_interleaved_with_another(void)
{
    const katherine_device_info_t info = {
        .status     = {.hw_type = 0x03, .hw_revision = 3, .serial_number = 1, .fw_version = 5},
        .chip_count = 1,
    };

    KT_REQUIRE(katherine_mutex_init(&g_link_mutex) == KATHERINE_E_OK);

    memset(&g_device, 0, sizeof(g_device));
    KT_REQUIRE(katherine_device_declare(&g_device, &info) == KATHERINE_E_OK);
    g_device.control_transport.ops = &STUB_OPS;

    g_mismatch[0] = 0;
    g_mismatch[1] = 0;

    kthread_t a, b;
    int first = 0, second = 1;
    KT_REQUIRE(kthread_start(&a, worker, &first) == 0);
    KT_REQUIRE(kthread_start(&b, worker, &second) == 0);
    KT_CHECK_EQ(kthread_join(&a), 0);
    KT_CHECK_EQ(kthread_join(&b), 0);

    KT_CHECK_EQ(g_mismatch[0], 0);
    KT_CHECK_EQ(g_mismatch[1], 0);

    g_device.control_transport.ops = NULL;
    katherine_mutex_fini(&g_link_mutex);
}

int
main(void)
{
    KT_RUN(test_an_exchange_is_not_interleaved_with_another);
    return kt_summary();
}
