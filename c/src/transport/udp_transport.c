/**
 * \file
 * \brief The UDP session, driven through the transport operations.
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include <katherine/transport.h>
#include <katherine/udp.h>

#include "protocol/cmd_interface.h"

// One buffer is enough here, which is the whole difference from the USB
// transport: a socket keeps filling SO_RCVBUF whether or not the application is
// inside recv(), so the kernel already does what a ring of outstanding reads
// does there. Lending a second buffer would buy nothing.
struct udp_transport {
    katherine_udp_t *udp;
    char *buffer;
    size_t capacity;
};

static katherine_error_t
udp_t_send_cmd(void *ctx, const void *data, size_t count)
{
    struct udp_transport *t = ctx;

    return katherine_udp_send_exact(t->udp, data, count);
}

static katherine_error_t
udp_t_recv_crd(void *ctx, void *crd, size_t count)
{
    struct udp_transport *t = ctx;

    return katherine_udp_recv_exact(t->udp, crd, count);
}

static katherine_error_t
udp_t_flush_incoming(void *ctx)
{
    struct udp_transport *t = ctx;

    // The library's own drain, rather than a second loop beside it: it is
    // bounded the same way, and it counts what it discarded where
    // katherine_udp_set_strict_ack() expects to find it.
    katherine_cmd_drain(t->udp);

    return KATHERINE_E_OK;
}

static katherine_error_t
udp_t_acquire_data_buf(void *ctx, void **buf, size_t *len)
{
    katherine_error_t res;
    struct udp_transport *t = ctx;
    size_t received         = t->capacity;

    res = katherine_udp_recv(t->udp, t->buffer, &received);
    if (res) goto err_recv;

    *buf = t->buffer;
    *len = received;

    return KATHERINE_E_OK;

err_recv:
    return res;
}

static katherine_error_t
udp_t_release_data_buf(void *ctx, void *buf)
{
    katherine_error_t res;
    struct udp_transport *t = ctx;

    if (buf != t->buffer) {
        res = KATHERINE_E_INVAL;
        goto err_foreign;
    }

    return KATHERINE_E_OK;

err_foreign:
    return res;
}

static katherine_error_t
udp_t_set_timeout(void *ctx, uint32_t timeout_ms)
{
    (void) ctx;
    (void) timeout_ms;

    // The session carries its own timeout, set when it was opened.
    return KATHERINE_E_OK;
}

static void
udp_t_close(void *ctx)
{
    struct udp_transport *t = ctx;
    if (t == NULL) return;

    free(t->buffer);
    free(t);
}

static const katherine_transport_ops_t udp_ops = {
    .send_cmd         = udp_t_send_cmd,
    .recv_crd         = udp_t_recv_crd,
    .flush_incoming   = udp_t_flush_incoming,
    .acquire_data_buf = udp_t_acquire_data_buf,
    .release_data_buf = udp_t_release_data_buf,
    .set_timeout      = udp_t_set_timeout,
    .close            = udp_t_close,
};

/**
 * Drive an open UDP session through the transport operations.
 * \param transport Transport to fill in.
 * \param udp Session to drive, which stays the caller's to close.
 * \param buffer_size Bytes one lent measurement-data buffer can carry.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport or no session was given, or the
 *   buffer is empty.
 * \retval KATHERINE_E_NOMEM if the transport or its buffer could not be
 *   allocated.
 */
katherine_error_t
katherine_transport_udp(katherine_transport_t *transport, katherine_udp_t *udp, size_t buffer_size)
{
    katherine_error_t res;

    if (transport == NULL || udp == NULL || buffer_size == 0) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    struct udp_transport *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_alloc;
    }

    t->buffer = malloc(buffer_size + sizeof(uint64_t));
    if (t->buffer == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_buffer;
    }

    t->udp      = udp;
    t->capacity = buffer_size;

    transport->ops  = &udp_ops;
    transport->kind = KATHERINE_TRANSPORT_UDP;
    transport->ctx  = t;

    return KATHERINE_E_OK;

err_buffer:
    free(t);
err_alloc:
err_args:
    return res;
}
