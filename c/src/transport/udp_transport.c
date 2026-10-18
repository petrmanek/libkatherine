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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/transport.h>
#include <katherine/udp.h>

#include "protocol/cmd_interface.h"

/**
 * What a session is opened at, before a reader names a size of its own with
 * katherine_transport_set_buffer_size(). Small on purpose: nothing reads
 * measurement data between a link being opened and an acquisition being set
 * up on it, so this is allocated and then replaced. Its job is to put an
 * allocation failure at open time rather than halfway through a measurement.
 */
#define DEFAULT_BUFFER_SIZE 4096

// One buffer is enough here, which is the whole difference from the USB
// transport: a socket keeps filling SO_RCVBUF whether or not the application
// is inside recv(), so the kernel already does what a ring of outstanding
// reads does there. Lending a second buffer would buy nothing.
//
// It is allocated a whole word past its capacity. A caller decoding 6-byte
// measurement data reads the last one through an 8-byte load, which reaches
// up to 2 bytes past what arrived; the slack keeps that read inside the
// allocation for any capacity, multiples of 8 included.
struct udp_transport {
    katherine_udp_t *udp;
    char *buffer;
    size_t capacity;
    bool lent; ///< True if the one buffer is in the caller's hands.
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

    t->lent = true;
    *buf    = t->buffer;
    *len    = received;

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

    t->lent = false;

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

static katherine_error_t
udp_t_set_buffer_size(void *ctx, size_t size)
{
    katherine_error_t res;
    struct udp_transport *t = ctx;

    if (size == 0) {
        res = KATHERINE_E_INVAL;
        goto err_size;
    }
    // The one buffer is the one that would move, so resizing under a caller
    // holding it would hand them a pointer into freed memory.
    if (t->lent) {
        res = KATHERINE_E_STATE;
        goto err_lent;
    }

    // Into a separate pointer, because realloc() leaves the original intact
    // when it fails and the transport then keeps reading at the size it had.
    char *resized = realloc(t->buffer, size + sizeof(uint64_t));
    if (resized == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_alloc;
    }

    t->buffer   = resized;
    t->capacity = size;

    return KATHERINE_E_OK;

err_alloc:
err_lent:
err_size:
    return res;
}

static void
udp_t_fini(void *ctx)
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
    .set_buffer_size  = udp_t_set_buffer_size,
    .fini             = udp_t_fini,
};

/**
 * Drive an open UDP session through the transport operations.
 * \param transport Transport to fill in.
 * \param udp Session to drive, which stays the caller's to close.
 *
 * The lent buffer starts at a small size of this backend's choosing. A
 * reader that needs a particular one asks for it with
 * katherine_transport_set_buffer_size().
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport or no session was given.
 * \retval KATHERINE_E_NOMEM if the transport or its buffer could not be
 *   allocated.
 */
katherine_error_t
katherine_transport_init_udp(katherine_transport_t *transport, katherine_udp_t *udp)
{
    katherine_error_t res;

    if (transport == NULL || udp == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    struct udp_transport *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_alloc;
    }

    t->buffer = malloc(DEFAULT_BUFFER_SIZE + sizeof(uint64_t));
    if (t->buffer == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_buffer;
    }

    t->udp      = udp;
    t->capacity = DEFAULT_BUFFER_SIZE;

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
