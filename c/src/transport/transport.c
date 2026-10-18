/**
 * \file
 * \brief Reaching a transport's operations, whichever backend supplies them.
 * \author Petr Mánek
 * \date 18.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/transport.h>

// Every operation is reached through one of these rather than through
// transport->ops, so that the vtable stays an implementation detail of the
// transports themselves. A caller that reaches past them is pinned to the
// present shape of the table and breaks when an operation is added, split or
// given a default.
//
// Each checks the transport before calling through it, because a zeroed
// katherine_transport_t -- which is what a device carries until a link is
// opened on it -- has a null table, and dereferencing it is the one mistake
// this layer exists to make impossible.
//
// Past that check each forwards whatever the transport reports, so the codes
// documented below are only the ones this layer raises itself; the rest are
// the opened transport's, and katherine_transport_udp() documents those.

/**
 * Send one command.
 * \param transport Transport to send on.
 * \param data Bytes of the command.
 * \param count Length of \p data in bytes.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, or it is not open.
 */
katherine_error_t
katherine_transport_send_cmd(katherine_transport_t *transport, const void *data, size_t count)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->send_cmd(transport->ctx, data, count);
    if (res) goto err_send;

    return KATHERINE_E_OK;

err_send:
err_transport:
    return res;
}

/**
 * Receive one command's response (CRD), of exactly \p count bytes.
 * \param transport Transport to receive on.
 * \param crd Storage for the response.
 * \param count Bytes to receive, which the response must match exactly.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, or it is not open.
 */
katherine_error_t
katherine_transport_recv_crd(katherine_transport_t *transport, void *crd, size_t count)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->recv_crd(transport->ctx, crd, count);
    if (res) goto err_recv;

    return KATHERINE_E_OK;

err_recv:
err_transport:
    return res;
}

/**
 * Discard whatever has already arrived. Returns at once, never waits.
 * \param transport Transport to discard on.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, or it is not open.
 */
katherine_error_t
katherine_transport_flush_incoming(katherine_transport_t *transport)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->flush_incoming(transport->ctx);
    if (res) goto err_flush;

    return KATHERINE_E_OK;

err_flush:
err_transport:
    return res;
}

/**
 * Lend the caller the next filled measurement-data buffer, waiting up to the
 * timeout set by katherine_transport_set_timeout(). The buffer stays the
 * transport's and is valid until katherine_transport_release_data_buf()
 * takes it back.
 * \param transport Transport to read from.
 * \param buf Storage for the lent buffer's address.
 * \param len Storage for its filled length in bytes.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, or it is not open.
 */
katherine_error_t
katherine_transport_acquire_data_buf(katherine_transport_t *transport, void **buf, size_t *len)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->acquire_data_buf(transport->ctx, buf, len);
    if (res) goto err_acquire;

    return KATHERINE_E_OK;

err_acquire:
err_transport:
    return res;
}

/**
 * Return a lent buffer, freeing the transport to fill it again.
 * \param transport Transport that lent it.
 * \param buf Buffer katherine_transport_acquire_data_buf() reported.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, it is not open, or the
 *   buffer is not one this transport lent.
 */
katherine_error_t
katherine_transport_release_data_buf(katherine_transport_t *transport, void *buf)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->release_data_buf(transport->ctx, buf);
    if (res) goto err_release;

    return KATHERINE_E_OK;

err_release:
err_transport:
    return res;
}

/**
 * Set how long a receive waits before reporting KATHERINE_E_TIMEOUT.
 * \param transport Transport to set the timeout on.
 * \param timeout_ms Limit on one receive, in milliseconds.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport was given, or it is not open.
 */
katherine_error_t
katherine_transport_set_timeout(katherine_transport_t *transport, uint32_t timeout_ms)
{
    katherine_error_t res;

    if (transport == NULL || transport->ops == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_transport;
    }

    res = transport->ops->set_timeout(transport->ctx, timeout_ms);
    if (res) goto err_timeout;

    return KATHERINE_E_OK;

err_timeout:
err_transport:
    return res;
}

/**
 * Release everything held by the transport, leaving it closed.
 * \param transport Transport to close, which may be closed already.
 */
void
katherine_transport_close(katherine_transport_t *transport)
{
    if (transport == NULL || transport->ops == NULL) return;

    transport->ops->close(transport->ctx);

    // Left closed rather than merely emptied: a second close is then the
    // no-op above instead of a second free, and a device that reopens a link
    // writes over a table that no longer points at a released context.
    transport->ops = NULL;
    transport->ctx = NULL;
}
