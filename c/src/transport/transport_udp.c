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

#include <katherine/device.h> // the discovery below fills the device's own result type
#include <katherine/transport.h>

#include "address.h"
#include "monoclock.h"
#include "msleep.h"
#include "repr_append.h"
#include "udp.h"
#include "protocol/cmd_interface.h"

/**
 * What a session is opened at, before a reader names a size of its own with
 * katherine_transport_set_buffer_size(). Small on purpose: nothing reads
 * measurement data between a link being opened and an acquisition being set
 * up on it, so this is allocated and then replaced. Its job is to put an
 * allocation failure at open time rather than halfway through a measurement.
 */
#define DEFAULT_BUFFER_SIZE 4096

// One buffer is enough, which is the whole difference from the USB transport:
// a socket keeps filling SO_RCVBUF whether or not the application is inside
// recv(), so the kernel already does what a ring does there.
//
// Allocated a whole word past its capacity: a decoder reads the last six-byte
// item through an eight-byte load, which reaches two bytes past what arrived.
typedef struct katherine_transport_udp {
    katherine_udp_t udp;
    char *buffer;
    size_t capacity;
    bool lent; ///< True if the one buffer is in the caller's hands.
} katherine_transport_udp_t;

/**
 * Send a command on the session.
 * \param ctx Session to send on.
 * \param data Bytes to send.
 * \param count How many.
 * \return KATHERINE_E_OK once every byte is on the wire.
 */
static katherine_error_t
udp_t_send_cmd(void *ctx, const void *data, size_t count)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_send_exact(&t->udp, data, count);
}

/**
 * Receive one command response.
 * \param ctx Session to read.
 * \param crd Storage for the response.
 * \param len Capacity on entry, bytes received on return.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_recv_crd(void *ctx, void *crd, size_t *len)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_recv(&t->udp, crd, len);
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

/**
 * Discard whatever an earlier exchange left on the session.
 * \param ctx Session to drain.
 * \param discarded Out-parameter for how many were dropped, or NULL.
 * \return KATHERINE_E_OK once the session is quiet.
 */
static katherine_error_t
udp_t_flush_incoming(void *ctx, uint32_t *discarded)
{
    katherine_transport_udp_t *t = ctx;

    udp_drain(&t->udp, discarded);

    return KATHERINE_E_OK;
}

/**
 * Lend the datagram just received.
 * \param ctx Session to read.
 * \param buf Out-parameter for the lent bytes.
 * \param len Out-parameter for how many.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_acquire_data_buf(void *ctx, void **buf, size_t *len)
{
    katherine_error_t res;
    katherine_transport_udp_t *t = ctx;
    size_t received              = t->capacity;

    res = katherine_udp_recv(&t->udp, t->buffer, &received);
    if (res) goto err_recv;

    t->lent = true;
    *buf    = t->buffer;
    *len    = received;

    return KATHERINE_E_OK;

err_recv:
    return res;
}

/**
 * Give back the lent datagram, which frees the one buffer to be filled again.
 * \param ctx Session it came from.
 * \param buf The buffer.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_release_data_buf(void *ctx, void *buf)
{
    katherine_error_t res;
    katherine_transport_udp_t *t = ctx;

    if (buf != t->buffer) {
        res = KATHERINE_E_INVAL;
        goto err_foreign;
    }

    t->lent = false;

    return KATHERINE_E_OK;

err_foreign:
    return res;
}

/**
 * Set how long a receive waits.
 * \param ctx Session to set.
 * \param timeout_ms The limit.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_set_timeout(void *ctx, uint32_t timeout_ms)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_set_timeout(&t->udp, timeout_ms);
}

/**
 * Datagrams the kernel dropped for want of queue space.
 * \param ctx Session to ask.
 * \param count Out-parameter for the running total.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_get_dropped_input(void *ctx, uint64_t *count)
{
    katherine_transport_udp_t *t = ctx;

    *count = katherine_udp_get_dropped_datagrams(&t->udp);

    return KATHERINE_E_OK;
}

/**
 * Take the lock serializing callers of this session.
 *
 * Delegated rather than reimplemented: the lock belongs to the session, and
 * anything else still reaching it directly takes the very same one.
 * \param ctx Session to lock.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_mutex_lock(void *ctx)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_mutex_lock(&t->udp);
}

/**
 * Release the lock udp_t_mutex_lock() took.
 * \param ctx Session to unlock.
 * \return KATHERINE_E_OK on success.
 */
static katherine_error_t
udp_t_mutex_unlock(void *ctx)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_mutex_unlock(&t->udp);
}


/**
 * The OS code the last failed operation reported.
 * \param ctx Session to ask.
 * \return The code, or 0 if nothing failed.
 */
static int
udp_t_get_last_os_error(void *ctx)
{
    katherine_transport_udp_t *t = ctx;

    return katherine_udp_last_os_error(&t->udp);
}

/**
 * Resize the buffer one datagram is received into.
 * \param ctx Session to resize.
 * \param size Bytes it is to hold.
 * \return KATHERINE_E_OK on success, leaving the old buffer in place otherwise.
 */
static katherine_error_t
udp_t_set_buffer_size(void *ctx, size_t size)
{
    katherine_error_t res;
    katherine_transport_udp_t *t = ctx;

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

/**
 * Close the session and free its buffer.
 * \param ctx Session to close.
 */
static void
udp_t_fini(void *ctx)
{
    katherine_transport_udp_t *t = ctx;
    if (t == NULL) return;

    katherine_udp_fini(&t->udp);
    free(t->buffer);
    free(t);
}

static const katherine_transport_ops_t udp_ops = {
    .send_cmd          = udp_t_send_cmd,
    .recv_crd          = udp_t_recv_crd,
    .flush_incoming    = udp_t_flush_incoming,
    .acquire_data_buf  = udp_t_acquire_data_buf,
    .release_data_buf  = udp_t_release_data_buf,
    .set_timeout       = udp_t_set_timeout,
    .set_buffer_size   = udp_t_set_buffer_size,
    .get_dropped_input = udp_t_get_dropped_input,
    .mutex_lock        = udp_t_mutex_lock,
    .mutex_unlock      = udp_t_mutex_unlock,
    .get_last_os_error = udp_t_get_last_os_error,
    .fini              = udp_t_fini,
};

/**
 * Open a UDP session and drive it through the transport operations.
 * \param transport Transport to fill in.
 * \param config Session to open, which the transport closes with itself.
 *
 * The lent buffer starts at a small size of this backend's choosing. A
 * reader that needs a particular one asks for it with
 * katherine_transport_set_buffer_size().
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if no transport or no configuration was given,
 *   or if the configuration names no remote address.
 * \retval KATHERINE_E_ADDRESS if either address could not be resolved, or the
 *   local one could not be bound; see inet_pton(3) and bind(2).
 * \retval KATHERINE_E_NOMEM if the transport or its buffer could not be
 *   allocated.
 * \retval KATHERINE_E_IO if opening the socket, sizing its receive queue or
 *   reading back the port it was given failed at the OS level for a reason
 *   none of the other codes name; see socket(2), setsockopt(2) and
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if one of those calls reported that it would
 *   have blocked, which this backend renders as a timeout whatever raised it;
 *   see katherine_transport_get_last_os_error() for the errno itself.
 * \retval KATHERINE_E_SYSTEM if the session's lock could not be created, or
 *   the socket failed for a reason outside the map; see
 *   pthread_mutex_init(3) and katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_transport_init_udp(katherine_transport_t *transport, const katherine_udp_config_t *config)
{
    katherine_error_t res;

    if (transport == NULL || config == NULL || config->remote_address == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    katherine_transport_udp_t *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_alloc;
    }

    t->buffer = malloc(DEFAULT_BUFFER_SIZE + sizeof(uint64_t));
    if (t->buffer == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_buffer;
    }

    res = katherine_udp_init_bound(&t->udp, config->local_address, config->local_port, config->remote_address,
        config->remote_port, config->timeout_ms);
    if (res != KATHERINE_E_OK) goto err_udp;

    if (config->pin_remote) katherine_udp_pin_remote(&t->udp);

    // Not every platform can attribute a dropped datagram to one session, and
    // one that cannot is still a session worth having.
    if (config->count_drops) (void) katherine_udp_set_drop_counting(&t->udp, true);

    // Asked for before anything arrives, since a queue resized under traffic
    // keeps whatever is already in it.
    if (config->rcvbuf != 0 && (res = katherine_udp_set_rcvbuf(&t->udp, config->rcvbuf)) != KATHERINE_E_OK)
        goto err_rcvbuf;

    t->capacity = DEFAULT_BUFFER_SIZE;

    transport->ops                     = &udp_ops;
    transport->kind                    = KATHERINE_TRANSPORT_UDP;
    transport->ctx                     = t;
    transport->strict_ack              = false;
    transport->stray_command_responses = 0;

    return KATHERINE_E_OK;

err_rcvbuf:
    katherine_udp_fini(&t->udp);
err_udp:
    free(t->buffer);
err_buffer:
    free(t);
err_alloc:
err_args:
    return res;
}

// ------------------------------------------------------------------
// Finding readouts on a network.
//
// A transport knows how to enumerate its own link, which is why this lives
// beside the UDP operations rather than in a module of its own: there is no
// discovery a second transport could share, only a second implementation.

/** Port the readouts listen on, and the one a firmware that ignores the
 * reply port answers to. The discovery sends here and binds it, so that
 * those answers have somewhere to land. */
#define DISCOVER_DEFAULT_PORT 1555

/** Pause between rounds of the collection loop. */
#define DISCOVER_POLL_MS      1

/** One socket's result from a single poll. */
typedef enum katherine_collect_result {
    KATHERINE_COLLECT_EMPTY,   ///< An empty socket.
    KATHERINE_COLLECT_DISCARD, ///< Something was waiting, but it is not an answer.
    KATHERINE_COLLECT_TAKEN,   ///< An answer, written to the entry.
} katherine_collect_result_t;

/**
 * Take one datagram from a socket, if one is waiting, and judge it.
 *
 * \param udp Session to poll
 * \param request The request as it was sent, for recognizing its own echo
 * \param legacy True for the socket bound to the standard port
 * \param entry Destination for an answer
 * \return What the poll found
 */
static katherine_collect_result_t
collect_one(katherine_udp_t *udp, const katherine_cmd_t *request, bool legacy, katherine_discovered_t *entry)
{
    char crd[KATHERINE_CMD_CRD_SIZE];
    size_t received = sizeof(crd);

    if (katherine_udp_recv_nowait(udp, crd, &received) != KATHERINE_E_OK) return KATHERINE_COLLECT_EMPTY;
    if (received != KATHERINE_CMD_CRD_SIZE) return KATHERINE_COLLECT_DISCARD;
    if ((uint8_t) crd[KATHERINE_CMD_OPCODE_BYTE] != CMD_TYPE_ECHO_CHIP_ID) return KATHERINE_COLLECT_DISCARD;

    // The request itself, delivered back to its sender: a broadcast reaches this
    // host too, and the operation code does not tell a command from its response.
    if (memcmp(crd, request->b, sizeof(request->b)) == 0) return KATHERINE_COLLECT_DISCARD;

    memset(entry, 0, sizeof(*entry));

    // Under an address that names the link as well as the readout, so that
    // what discovery reports is what katherine_device_init() takes.
    char host[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
    if (katherine_udp_get_remote_address(udp, host, sizeof(host)) != KATHERINE_E_OK) {
        return KATHERINE_COLLECT_DISCARD;
    }

    const int written = katherine_address_format(entry->address, sizeof(entry->address), KATHERINE_TRANSPORT_UDP, host);
    if (written < 0 || (size_t) written >= sizeof(entry->address)) {
        return KATHERINE_COLLECT_DISCARD;
    }
    entry->legacy = legacy;

    return KATHERINE_COLLECT_TAKEN;
}

/**
 * Ask every readout reachable at an address to identify itself.
 *
 * Sends one echo-chip-id command and records the address of every readout
 * that answers. Aimed at a broadcast address it finds every readout on the
 * subnet; aimed at one host it is a liveness check for that host.
 *
 * Two sockets receive, and which one an answer arrives on is the firmware
 * generation. The request names \p reply_port in its first two bytes; a
 * firmware that reads it answers there, and one that does not answers on
 * the standard port, which this call binds for exactly that reason. The
 * second case sets katherine_discovered_t::legacy.
 *
 * \note The standard port is bound for the duration, so a discovery cannot
 *   run while a session on this host holds it -- which katherine_device_init()
 *   does for its control socket.
 *
 * \note The timeout bounds the call. Datagrams that are not answers are
 *   discarded, at most KATHERINE_CMD_MAX_STRAY_DISCARDS of them, so a port
 *   carrying other traffic cannot stretch it either. Choose it accordingly
 *   -- a subnet's worth of readouts answer within milliseconds of each
 *   other, so a few hundred milliseconds is generous.
 *
 * \note Duplicate answers are kept. A readout on a host with two addresses
 *   answers twice, from both, and which of them a caller should talk to is
 *   not this function's to decide.
 *
 * \param address Address to ask, broadcast or unicast, optionally naming the
 *   link it is on: "udp://192.168.1.255". An address with no scheme is a UDP
 *   address, and one naming another link is refused, this asking over UDP
 * \param reply_port Port a current firmware is to answer on; at least
 *   KATHERINE_DISCOVER_MIN_PORT, and not the standard port itself
 * \param timeout_ms Limit on the whole call, in milliseconds; at least one
 * \param out Storage for the answers, of at least the entries \p count gives
 * \param count On entry the capacity of \p out, on return the number of
 *   entries written, which is zero on every error
 *
 * \retval KATHERINE_E_OK on success, including when nothing answered.
 * \retval KATHERINE_E_INVAL if out or count is NULL, the capacity count
 *   gives is zero, the timeout is zero, the reply port is below
 *   KATHERINE_DISCOVER_MIN_PORT or is the standard port, or a socket call
 *   rejected an argument; see sendto(2) and socket(2).
 * \retval KATHERINE_E_ADDRESS if the address names a link that is not UDP or one
 *   this library does not know, is a scheme and nothing else, could not be
 *   parsed, or either local port could not be bound; see inet_pton(3) and
 *   bind(2).
 * \retval KATHERINE_E_TIMEOUT if the request could not be sent because the
 *   send would have blocked; see sendto(2). Not returned for want of an
 *   answer -- a discovery that hears nothing succeeds with none found.
 * \retval KATHERINE_E_IO if a send or a socket setup call failed at the OS
 *   level for a reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a socket call ran out of memory.
 * \retval KATHERINE_E_SYSTEM if a session's lock could not be created.
 */
katherine_error_t
katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
{
    katherine_error_t res;
    katherine_transport_kind_t kind;
    const char *host;

    if (out == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_out;
    }
    if (count == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_count;
    }

    const size_t capacity = *count;
    *count                = 0;

    if (capacity == 0) {
        res = KATHERINE_E_INVAL;
        goto err_capacity;
    }
    if (reply_port < KATHERINE_DISCOVER_MIN_PORT || reply_port == DISCOVER_DEFAULT_PORT) {
        res = KATHERINE_E_INVAL;
        goto err_port;
    }
    // Zero reads as no timeout at all -- SO_RCVTIMEO is left unset and a receive
    // blocks past this call's own deadline -- and a millisecond is the shortest
    // limit the parameter can express, so anything under one is refused.
    if (timeout_ms == 0) {
        res = KATHERINE_E_INVAL;
        goto err_timeout;
    }

    // This asks over UDP whatever the address says, so a scheme naming
    // another link is an address for somebody else's discovery.
    res = katherine_address_split(address, &kind, &host);
    if (res) goto err_address;
    if (kind != KATHERINE_TRANSPORT_UDP) {
        res = KATHERINE_E_ADDRESS;
        goto err_kind;
    }

    // Both unpinned on purpose: a pinned session would discard every answer but the
    // first host's, and adopting each sender in turn is how each address is read back.
    //
    // The standard port is bound first because the request is sent from it, which
    // makes a firmware's two possible replies land on the same socket.
    katherine_udp_t legacy;
    res = katherine_udp_init_bound(&legacy, NULL, DISCOVER_DEFAULT_PORT, host, DISCOVER_DEFAULT_PORT, timeout_ms);
    if (res) goto err_legacy;

    katherine_udp_t modern;
    res = katherine_udp_init_bound(&modern, NULL, reply_port, host, DISCOVER_DEFAULT_PORT, timeout_ms);
    if (res) goto err_modern;

    res = katherine_udp_set_broadcast(&legacy, true);
    if (res) goto err_broadcast;

    // The reply port travels in the payload, where the firmware reads it.
    katherine_cmd_t request = katherine_cmd_create(CMD_TYPE_ECHO_CHIP_ID);
    katherine_cmd_payload_set_i64(&request, (int64_t) reply_port);

    // Sent on the session directly: discovery is a broadcast by nature and opens no
    // transport over either of its sessions.
    res = katherine_udp_send_exact(&legacy, request.b, sizeof(request.b));
    if (res) goto err_send;

    const uint64_t deadline = katherine_clock_monotonic_ns() + (uint64_t) timeout_ms * 1000000ULL;
    uint32_t discarded      = 0;

    while (*count < capacity && discarded < KATHERINE_CMD_MAX_STRAY_DISCARDS) {
        bool idle = true;

        for (size_t i = 0; i < 2 && *count < capacity; ++i) {
            const bool is_legacy = (i == 0);
            katherine_udp_t *udp = is_legacy ? &legacy : &modern;

            switch (collect_one(udp, &request, is_legacy, &out[*count])) {
            case KATHERINE_COLLECT_TAKEN:
                ++*count;
                idle = false;
                break;
            case KATHERINE_COLLECT_DISCARD:
                ++discarded;
                idle = false;
                break;
            case KATHERINE_COLLECT_EMPTY:
                break;
            }
        }

        if (idle) {
            if (katherine_clock_monotonic_ns() >= deadline) break;
            katherine_msleep(DISCOVER_POLL_MS);
        }
    }

    katherine_udp_fini(&modern);
    katherine_udp_fini(&legacy);
    return KATHERINE_E_OK;

err_send:
err_broadcast:
    katherine_udp_fini(&modern);
err_modern:
    katherine_udp_fini(&legacy);
err_legacy:
err_kind:
err_address:
err_timeout:
err_port:
err_capacity:
err_count:
err_out:
    return res;
}

/**
 * The local port a UDP transport's session is bound to.
 * \param transport Transport to ask, open on the UDP link.
 * \param port Storage for the port, in host byte order, or NULL to ask only
 *   whether the link has one.
 *
 * Off the generic interface because no other link has a port space, and an
 * operation only one backend can answer does not belong in a table every
 * backend fills in.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_UNSUPPORTED if the transport is open on a link with no
 *   port space, which is every link but UDP.
 */
katherine_error_t
katherine_transport_udp_bound_port(const katherine_transport_t *transport, uint16_t *port)
{
    katherine_error_t res;

    if (transport->kind != KATHERINE_TRANSPORT_UDP) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_kind;
    }

    const katherine_transport_udp_t *t = transport->ctx;

    if (port != NULL) {
        *port = ntohs(t->udp.address_local.sin_port);
    }

    return KATHERINE_E_OK;

err_kind:
    return res;
}

/**
 * Write "A.B.C.D:port" from a sockaddr_in.
 *
 * Hand-formatted rather than through inet_ntop(), which Windows declares in a
 * different header than POSIX does; sin_addr and sin_port are named the same on
 * both.
 * \param buf Destination, at least 22 bytes for "255.255.255.255:65535".
 * \param cap Its capacity.
 * \param address The endpoint to render.
 */
static void
format_ipv4_port(char *buf, size_t cap, const struct sockaddr_in *address)
{
    uint32_t host_address = ntohl(address->sin_addr.s_addr);
    uint16_t host_port    = ntohs(address->sin_port);

    snprintf(buf, cap, "%u.%u.%u.%u:%u", (unsigned) ((host_address >> 24) & 0xFFu), (unsigned) ((host_address >> 16) & 0xFFu),
        (unsigned) ((host_address >> 8) & 0xFFu), (unsigned) (host_address & 0xFFu), (unsigned) host_port);
}

/**
 * Render a UDP session's endpoints, pin state and command-response
 * correlation state. The socket handle and the mutex are omitted: neither is
 * meaningful in a log line.
 * \copydetails katherine_tpx3_coord_snprint
 */
int
katherine_udp_snprint(char *buf, size_t cap, const katherine_udp_t *v)
{
    char local[24];
    char remote[24];
    size_t off = 0;

    format_ipv4_port(local, sizeof(local), &v->address_local);
    format_ipv4_port(remote, sizeof(remote), &v->address_remote);

    REPR_APPENDF(buf, cap, off, "udp{local: %s, remote: %s, pinned: %s, last_os_error: %d}", local, remote,
        katherine_str_bool(v->remote_pinned), v->last_os_error);
    return (int) off;
}
