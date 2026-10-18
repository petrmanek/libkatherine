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
// Each is the call and nothing else. The transport is the caller's to have
// opened, exactly as katherine_udp_send_exact() takes a session the caller
// opened, and this layer does not re-check what the rest of the library
// leaves unchecked either.
//
// The \retval lists below give only what this layer can say. Which codes a
// call actually produces belongs to the transport behind it, and gathering
// those from udp_transport.c -- and from usb_transport.c once it exists --
// into these blocks is work still to do.

/**
 * Send one command.
 * \param transport Transport to send on.
 * \param data Bytes of the command.
 * \param count Length of \p data in bytes.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_send_cmd(katherine_transport_t *transport, const void *data, size_t count)
{
    return transport->ops->send_cmd(transport->ctx, data, count);
}

/**
 * Receive one command's response (CRD), of exactly \p count bytes.
 * \param transport Transport to receive on.
 * \param crd Storage for the response.
 * \param count Bytes to receive, which the response must match exactly.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_recv_crd(katherine_transport_t *transport, void *crd, size_t count)
{
    return transport->ops->recv_crd(transport->ctx, crd, count);
}

/**
 * Discard whatever has already arrived. Returns at once, never waits.
 * \param transport Transport to discard on.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_flush_incoming(katherine_transport_t *transport)
{
    return transport->ops->flush_incoming(transport->ctx);
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
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_acquire_data_buf(katherine_transport_t *transport, void **buf, size_t *len)
{
    return transport->ops->acquire_data_buf(transport->ctx, buf, len);
}

/**
 * Return a lent buffer, freeing the transport to fill it again.
 * \param transport Transport that lent it.
 * \param buf Buffer katherine_transport_acquire_data_buf() reported.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_release_data_buf(katherine_transport_t *transport, void *buf)
{
    return transport->ops->release_data_buf(transport->ctx, buf);
}

/**
 * Set how long a receive waits before reporting KATHERINE_E_TIMEOUT.
 * \param transport Transport to set the timeout on.
 * \param timeout_ms Limit on one receive, in milliseconds.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_set_timeout(katherine_transport_t *transport, uint32_t timeout_ms)
{
    return transport->ops->set_timeout(transport->ctx, timeout_ms);
}

/**
 * Make one lent buffer carry exactly \p size bytes, or fail.
 *
 * Exact rather than advisory, so that a caller which sized something else to
 * match still knows the capacity afterwards. A transport whose buffers the
 * link fixes refuses outright rather than settling on another size.
 * \param transport Transport to resize.
 * \param size Bytes one lent buffer is to carry, at least one.
 *
 * \retval KATHERINE_E_OK on success, the capacity now being exactly \p size.
 * \retval KATHERINE_E_UNSUPPORTED if this transport's buffers are fixed.
 * \retval KATHERINE_E_STATE if a buffer is lent out, which resizing would
 *   leave dangling in the caller's hands.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_set_buffer_size(katherine_transport_t *transport, size_t size)
{
    return transport->ops->set_buffer_size(transport->ctx, size);
}

/**
 * Take the lock excluding everything else that drives this transport.
 *
 * Held across a request and its response, and across a whole read loop, so
 * that two callers cannot interleave on one link.
 * \param transport Transport to lock.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_mutex_lock(katherine_transport_t *transport)
{
    return transport->ops->mutex_lock(transport->ctx);
}

/**
 * Release the lock taken by katherine_transport_mutex_lock().
 * \param transport Transport to unlock.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval ANY The transport's own codes, beyond those listed here; see the
 *   transport this one was opened as.
 */
katherine_error_t
katherine_transport_mutex_unlock(katherine_transport_t *transport)
{
    return transport->ops->mutex_unlock(transport->ctx);
}

/**
 * Release everything held by the transport.
 * \param transport Transport to release.
 */
void
katherine_transport_fini(katherine_transport_t *transport)
{
    transport->ops->fini(transport->ctx);
}
