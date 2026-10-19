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
udp_t_recv_crd(void *ctx, void *crd, size_t *len)
{
    struct udp_transport *t = ctx;

    return katherine_udp_recv(t->udp, crd, len);
}

/**
 * Discard whatever a previous exchange left queued on a command session.
 *
 * The readout answers some commands more than once and the network may
 * deliver a response the client had already given up on, so a session can
 * hold datagrams belonging to no request in flight. Correlation recognizes
 * those and steps over them, but a leftover response of the *same* command
 * would correlate perfectly and be read as the answer to a later repetition
 * of it, which is how a session ends up one command out of step for good.
 * Flushing before a command closes that gap, and keeps the stray budget of
 * the wait that follows for genuine surprises.
 *
 * Never blocks: it stops at the first receive that finds nothing queued,
 * which is the ordinary case and must cost nothing, since every command of
 * the library pays for it. Best-effort by design and therefore returning
 * nothing -- a transport failure here resurfaces immediately at the send or
 * receive that follows, where the caller is already prepared for it. What
 * was discarded is reported back, for the transport to add to
 * katherine_transport_t::stray_command_responses.
 *
 * \param udp Session to flush.
 * \param discarded Storage for how many datagrams were thrown away.
 */
static void
udp_drain(katherine_udp_t *udp, uint32_t *discarded)
{
    // One byte of headroom, so that an oversized datagram is observed as
    // oversized rather than silently truncated to a plausible length.
    char crd[KATHERINE_CMD_CRD_SIZE + 1];

    for (uint32_t taken = 0; taken < KATHERINE_CMD_MAX_DRAIN; ++taken) {
        size_t received = sizeof(crd);

        if (katherine_udp_recv_nowait(udp, crd, &received) != 0) return;

        ++*discarded;
    }
}

static katherine_error_t
udp_t_flush_incoming(void *ctx, uint32_t *discarded)
{
    struct udp_transport *t = ctx;

    udp_drain(t->udp, discarded);

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
    struct udp_transport *t = ctx;

    return katherine_udp_set_timeout(t->udp, timeout_ms);
}

static katherine_error_t
udp_t_get_bound_udp_port(void *ctx, uint16_t *port)
{
    struct udp_transport *t = ctx;

    *port = ntohs(t->udp->address_local.sin_port);

    return KATHERINE_E_OK;
}

// Delegated rather than reimplemented: the lock belongs to the session, and
// anything else still reaching that session directly -- the command layer,
// until it too goes through a transport -- takes the very same one.
static katherine_error_t
udp_t_mutex_lock(void *ctx)
{
    struct udp_transport *t = ctx;

    return katherine_udp_mutex_lock(t->udp);
}

static katherine_error_t
udp_t_mutex_unlock(void *ctx)
{
    struct udp_transport *t = ctx;

    return katherine_udp_mutex_unlock(t->udp);
}


static int
udp_t_last_os_error(void *ctx)
{
    struct udp_transport *t = ctx;

    return katherine_udp_last_os_error(t->udp);
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
    .send_cmd           = udp_t_send_cmd,
    .recv_crd           = udp_t_recv_crd,
    .flush_incoming     = udp_t_flush_incoming,
    .acquire_data_buf   = udp_t_acquire_data_buf,
    .release_data_buf   = udp_t_release_data_buf,
    .set_timeout        = udp_t_set_timeout,
    .set_buffer_size    = udp_t_set_buffer_size,
    .get_bound_udp_port = udp_t_get_bound_udp_port,
    .mutex_lock         = udp_t_mutex_lock,
    .mutex_unlock       = udp_t_mutex_unlock,
    .last_os_error      = udp_t_last_os_error,
    .fini               = udp_t_fini,
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

    transport->ops                     = &udp_ops;
    transport->kind                    = KATHERINE_TRANSPORT_UDP;
    transport->ctx                     = t;
    transport->strict_ack              = false;
    transport->stray_command_responses = 0;

    return KATHERINE_E_OK;

err_buffer:
    free(t);
err_alloc:
err_args:
    return res;
}
