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
 * \retval KATHERINE_E_TIMEOUT if the link would have blocked rather than
 *   carry the message within the timeout set by
 *   katherine_transport_set_timeout().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if the link failed at the OS level for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_INVAL if the link rejected an argument of the
 *   transfer; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_NOMEM if the link had no memory to carry it; see
 *   katherine_transport_last_os_error().
 */
katherine_error_t
katherine_transport_send_cmd(katherine_transport_t *transport, const void *data, size_t count)
{
    return transport->ops->send_cmd(transport->ctx, data, count);
}

/**
 * Receive one command's response (CRD).
 * \param transport Transport to receive on.
 * \param crd Storage for the response.
 * \param len On entry the capacity of \p crd, on return what arrived.
 *
 * The capacity is given with room to spare so that a response longer than a
 * CRD is seen to be longer, rather than truncated to a plausible length.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the link would have blocked rather than
 *   carry the message within the timeout set by
 *   katherine_transport_set_timeout().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if the link failed at the OS level for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_INVAL if the link rejected an argument of the
 *   transfer; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_NOMEM if the link had no memory to carry it; see
 *   katherine_transport_last_os_error().
 */
katherine_error_t
katherine_transport_recv_crd(katherine_transport_t *transport, void *crd, size_t *len)
{
    return transport->ops->recv_crd(transport->ctx, crd, len);
}

/**
 * Discard whatever has already arrived. Returns at once, never waits.
 * \param transport Transport to discard on.
 *
 * \retval KATHERINE_E_OK on success.
 */
katherine_error_t
katherine_transport_flush_incoming(katherine_transport_t *transport)
{
    uint32_t discarded = 0;

    // Tallied here rather than by the backend, which is handed a context and
    // never the transport the count lives on. One place counts, so a link
    // that discards nothing simply reports nothing.
    const katherine_error_t res = transport->ops->flush_incoming(transport->ctx, &discarded);

    transport->stray_command_responses += discarded;

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
 * \retval KATHERINE_E_TIMEOUT if the link would have blocked rather than
 *   carry the message within the timeout set by
 *   katherine_transport_set_timeout().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if the link failed at the OS level for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_INVAL if the link rejected an argument of the
 *   transfer; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_NOMEM if the link had no memory to carry it; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_STATE if the link has no read outstanding and cannot
 *   post one, so waiting could only ever time out. A transport that keeps a
 *   ring of reads going reports this when the whole of it has come back
 *   empty and none could be reposted; one that reads on demand has no such
 *   state to be in.
 * \retval KATHERINE_E_SYSTEM if the link's lock could not be taken or
 *   released, which a transport serializing its own access can report;
 *   see katherine_transport_last_os_error().
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
 * \retval KATHERINE_E_INVAL if the buffer is not one this transport lent.
 * \retval KATHERINE_E_SYSTEM if the link's lock could not be taken or
 *   released, which a transport serializing its own access can report;
 *   see katherine_transport_last_os_error().
 * \note Giving a buffer back does not put it back to work. A transport that
 *   keeps reads outstanding posts the next one when something asks for the
 *   next buffer, not here, so a submission's failures belong to
 *   katherine_transport_acquire_data_buf() rather than to this call.
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
 * \retval KATHERINE_E_INVAL if the link rejected the limit; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if setting it failed at the OS level for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT, which
 *   reach this list only through the shared error map; neither setsockopt(2)
 *   nor its Winsock counterpart documents a condition mapping to them.
 * \retval KATHERINE_E_SYSTEM if the link's lock could not be taken or
 *   released, which a transport serializing its own access can report;
 *   see katherine_transport_last_os_error().
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
 * \retval KATHERINE_E_INVAL if the size is zero.
 * \retval KATHERINE_E_NOMEM if the new buffer could not be allocated, the
 *   transport keeping the one it had.
 * \note No transport yet refuses outright, so KATHERINE_E_UNSUPPORTED is
 *   documented ahead of the first whose buffers a link fixes. The index
 *   reports it unreachable until then, which is the honest state.
 * \retval KATHERINE_E_SYSTEM if the link's lock could not be taken or
 *   released, which a transport serializing its own access can report;
 *   see katherine_transport_last_os_error().
 */
katherine_error_t
katherine_transport_set_buffer_size(katherine_transport_t *transport, size_t size)
{
    return transport->ops->set_buffer_size(transport->ctx, size);
}

/**
 * Input the link lost before this library read it.
 * \param transport Link to ask.
 * \param count Datagrams, or whatever unit the link delivers, lost for want
 *   of room in its own receive queue since the count was switched on.
 *
 * Counts only what the host lost, and only on a link that keeps a queue of
 * its own. What a readout dropped before transmitting is its to report; see
 * katherine_frame_info_t::lost_pixels.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_UNSUPPORTED on a link that keeps no such queue, or one
 *   whose platform cannot attribute a loss to one session.
 */
katherine_error_t
katherine_transport_get_dropped_input(const katherine_transport_t *transport, uint64_t *count)
{
    return transport->ops->get_dropped_input(transport->ctx, count);
}

/**
 * Take the lock excluding everything else that drives this transport.
 *
 * Held across a request and its response, and across a whole read loop, so
 * that two callers cannot interleave on one link.
 * \param transport Transport to lock.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the link's lock was not in a lockable
 *   state, which on a session means one never opened or already
 *   finalized; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the lock could not be taken for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 * \note Unreachable through the UDP transport: KATHERINE_E_NOMEM and
 *   KATHERINE_E_TIMEOUT. pthread_mutex_lock(3) documents EBUSY, EDEADLK,
 *   EINVAL and EPERM and no more, so neither can arrive however the map
 *   behind it is capable of producing them.
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
 * \retval KATHERINE_E_INVAL if the link's lock was not in a releasable
 *   state, which on a session means one never opened or already
 *   finalized; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the lock could not be released for a reason
 *   none of the other codes cover; see katherine_transport_last_os_error().
 * \note Unreachable through the UDP transport: KATHERINE_E_NOMEM and
 *   KATHERINE_E_TIMEOUT. pthread_mutex_unlock(3) documents EBUSY, EDEADLK,
 *   EINVAL and EPERM and no more, so neither can arrive however the map
 *   behind it is capable of producing them.
 */
katherine_error_t
katherine_transport_mutex_unlock(katherine_transport_t *transport)
{
    return transport->ops->mutex_unlock(transport->ctx);
}

/**
 * The OS-level error behind the last failure, or 0.
 *
 * What a KATHERINE_E_IO, KATHERINE_E_NOMEM or KATHERINE_E_SYSTEM was
 * underneath. The number is the link's own -- an errno on a socket, a
 * libusb code on a bus -- so it is read with the transport's kind in hand.
 * \param transport Transport to ask.
 *
 * \return The code, or 0 where the transport has recorded none.
 */
int
katherine_transport_last_os_error(const katherine_transport_t *transport)
{
    return transport->ops->last_os_error(transport->ctx);
}

/**
 * Render a transport kind as the token naming it.
 * \param kind Kind to render.
 *
 * \return The token, or "?" for a value this version does not name.
 */
const char *
katherine_str_transport_kind(katherine_transport_kind_t kind)
{
    switch (kind) {
    case KATHERINE_TRANSPORT_UDP: return "udp";
    case KATHERINE_TRANSPORT_USB: return "usb";
    default:                      return "?";
    }
}

/**
 * Require a command response to repeat the operation code of its request
 * exactly.
 *
 * Off by default, because a readout answers some commands with a reply
 * identifier derived from the request's rather than the request's own, and
 * correlation accepts either. Turning this on narrows what counts as an
 * answer and widens what is counted as a stray.
 * \param transport Transport to set it on.
 * \param strict True to require the exact operation code.
 */
void
katherine_transport_set_strict_ack(katherine_transport_t *transport, bool strict)
{
    transport->strict_ack = strict;
}

/**
 * Release everything held by the transport.
 * \param transport Transport to release.
 */
void
katherine_transport_fini(katherine_transport_t *transport)
{
    // Guard against double-free. The operation table says whether there is
    // anything to release.
    if (transport->ops == NULL) return;

    transport->ops->fini(transport->ctx);

    transport->ops = NULL;
    transport->ctx = NULL;
}
