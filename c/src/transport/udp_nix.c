/**
 * \file
 * \brief POSIX implementation of the UDP communication layer.
 * \author Petr Mánek
 * \date 29.5.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// For KATHERINE_DEVICE_ADDRESS_STR_SIZE, the buffer an address is written into.
#include <katherine/device.h>
#include <katherine/global.h>

#if defined(KATHERINE_FEATURE_UDP) && defined(KATHERINE_NIX)

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <string.h>
#include <katherine/error.h>
#include "udp.h"

#include "transport/udp_error_map.h"

#ifdef KATHERINE_DEBUG_UDP
static inline void
dump_buffer(const char *msg, const uint8_t *buf, size_t count)
{
    printf("%-10s ", msg);

#if KATHERINE_DEBUG_UDP >= 2
    if (count < 60000) {
        for (size_t i = 0; i < count; i++) {
            printf("%02X ", buf[i]);
            if (count != 8 && i % 6 == 5) {
                printf("\n");
            }
        }
        printf("\n");
    }
#endif

    printf("(%ld bytes)\n", count);
}
#endif /* KATHERINE_DEBUG_UDP */

/**
 * True if a datagram received from address counts as coming from the pinned
 * remote of session u.
 *
 * Only the host is compared, never the port: a readout answers commands from
 * its command port but streams measurement data from another one (1556 vs
 * 1555 for the emulated readout), and no source port of the firmware is
 * specified anywhere, whereas the hazard a pin guards against -- another
 * peer's stray datagram becoming the session's remote -- is a property of
 * the host.
 *
 * \param u UDP session holding the pin
 * \param address Source address of the datagram just received
 * \return true if the datagram counts as coming from the pinned remote.
 */
static bool
from_pinned_remote(const katherine_udp_t *u, const struct sockaddr_in *address)
{
    return address->sin_addr.s_addr == u->address_remote.sin_addr.s_addr;
}

/**
 * Reports a failed receive, recording the OS-level detail only when there is
 * any: an empty non-blocking socket and an expired receive timeout are
 * ordinary outcomes rather than faults, so neither leaves an errno behind for
 * katherine_udp_last_os_error() to hand out. The Windows readiness check
 * clears it for the same reason.
 *
 * Deliberately duplicated in udp_win.c rather than shared. The bodies are
 * identical, but nothing else these two files have in common is -- the two
 * receive loops differ substantially and from_pinned_remote() spells its
 * argument type differently -- so a header for one small function would earn
 * less than it costs, and each copy gets to name its own platform's codes.
 *
 * \param u UDP session
 * \param err errno of the failed syscall
 *
 * \retval KATHERINE_E_TIMEOUT if err says the receive found nothing --
 *   EAGAIN, EWOULDBLOCK or ETIMEDOUT -- which is the expired receive
 *   timeout of an idle socket, or the EAGAIN of a MSG_DONTWAIT receive on an
 *   empty one. This is the case that leaves last_os_error cleared.
 * \retval KATHERINE_E_INVAL if err is EINVAL; see recvfrom(2).
 * \retval KATHERINE_E_NOMEM if err is ENOMEM; see recvfrom(2).
 * \retval KATHERINE_E_IO for any other errno, this function's fallback.
 * \retval KATHERINE_E_OK never: this is a failure path, and err is the errno
 *   of a syscall that has already failed.
 */
static katherine_error_t
recv_failure(katherine_udp_t *u, int err)
{
    katherine_error_t mapped = katherine_udp_map_socket_error(err, KATHERINE_E_IO);

    u->last_os_error = (mapped == KATHERINE_E_TIMEOUT) ? 0 : err;
    return mapped;
}

/**
 * Receives one datagram and, where the session is counting drops, reads the
 * kernel's own drop counter out of the ancillary data that accompanies it.
 *
 * recvmsg() rather than recvfrom() only when asked: the control-message
 * walk is per datagram, and the data path takes millions of them.
 *
 * The kernel's counter is cumulative over the socket's life and is 32 bits
 * wide, so what is accumulated here is the difference between successive
 * readings -- which survives the wrap, the difference being correct modulo
 * 2^32 as long as fewer than that many drops fall between two datagrams.
 *
 * \param u UDP session to receive on
 * \param data Buffer to receive into
 * \param count Buffer size in bytes
 * \param from Set to the sender's address
 * \param flags recvfrom(2) flags
 * \return What the underlying receive returned, unchanged.
 */
static ssize_t
recv_one(katherine_udp_t *u, void *data, size_t count, struct sockaddr_in *from, int flags)
{
    if (!u->count_drops) {
        socklen_t address_len = sizeof(*from);
        return recvfrom(u->sock, data, count, flags, (struct sockaddr *) from, &address_len);
    }

#ifdef SO_RXQ_OVFL
    struct iovec iov = {.iov_base = data, .iov_len = count};
    union {
        struct cmsghdr align;
        char bytes[CMSG_SPACE(sizeof(uint32_t))];
    } control;
    struct msghdr msg = {
        .msg_name       = from,
        .msg_namelen    = sizeof(*from),
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = control.bytes,
        .msg_controllen = sizeof(control.bytes),
    };

    ssize_t res = recvmsg(u->sock, &msg, flags);
    if (res == -1) return res;

    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c != NULL; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SO_RXQ_OVFL) continue;

        uint32_t dropped;
        memcpy(&dropped, CMSG_DATA(c), sizeof(dropped));
        u->dropped_datagrams += (uint32_t) (dropped - u->last_drop_count);
        u->last_drop_count = dropped;
    }

    return res;
#else
    socklen_t address_len = sizeof(*from);
    return recvfrom(u->sock, data, count, flags, (struct sockaddr *) from, &address_len);
#endif /* SO_RXQ_OVFL */
}

/**
 * Receives one datagram from the pinned remote of session u, discarding up to
 * KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts on the way there.
 * Spending that budget is reported as KATHERINE_E_TIMEOUT, the very code
 * the expired receive timeout of an idle socket yields, so that no caller
 * needs a separate path for it.
 *
 * flags is passed straight to recvfrom(), which is how MSG_DONTWAIT reaches
 * it for katherine_udp_recv_nowait(); the EAGAIN an empty socket then
 * returns maps to the same KATHERINE_E_TIMEOUT a spent timeout does.
 *
 * The pinned address is read from address_remote itself rather than from a copy
 * taken when the pin was placed, which is what makes the pin follow
 * katherine_udp_set_remote().
 *
 * \param u UDP session to receive on
 * \param data Buffer to receive into
 * \param count Buffer size in bytes
 * \param received Set to the number of bytes received, on success
 * \param flags recvfrom(2) flags; MSG_DONTWAIT for a non-blocking receive
 *
 * \retval KATHERINE_E_OK on success, with *received set.
 * \retval KATHERINE_E_TIMEOUT if no datagram arrived within the session's
 *   receive timeout, or if the discard budget was spent on datagrams from
 *   other hosts. Both leave last_os_error cleared; see recv_failure().
 * \retval KATHERINE_E_INVAL if recvfrom(2) rejected an argument of the
 *   receive; see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel had no memory for the receive; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO for any other errno; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 */
static katherine_error_t
recv_pinned(katherine_udp_t *u, void *data, size_t count, size_t *received, int flags)
{
    for (uint32_t discarded = 0; discarded < KATHERINE_UDP_PIN_MAX_DISCARDS; ++discarded) {
        struct sockaddr_in address_from;
        ssize_t res = recv_one(u, data, count, &address_from, flags);
        if (res == -1) {
            return recv_failure(u, errno);
        }

        if (from_pinned_remote(u, &address_from)) {
            *received = (size_t) res;
            return KATHERINE_E_OK;
        }
    }

    // The discard budget is spent, not an OS-level failure -- reported
    // exactly like an expired receive timeout (see KATHERINE_UDP_PIN_MAX_DISCARDS
    // above), so no caller needs a separate path for it.
    u->last_os_error = 0;
    return KATHERINE_E_TIMEOUT;
}

/**
 * Receives one datagram into data, honoring the pin of session u: a pinned
 * session accepts only datagrams from its remote host and leaves address_remote
 * alone, an unpinned one accepts the next datagram from anybody and adopts
 * its sender as the remote -- the server behavior of replying to whoever
 * asked last.
 *
 * \param u UDP session to receive on
 * \param data Buffer to receive into
 * \param count Buffer size in bytes
 * \param received Set to the number of bytes received, on success
 * \param flags recvfrom(2) flags; MSG_DONTWAIT for a non-blocking receive
 *
 * \retval KATHERINE_E_OK on success, with *received set.
 * \retval KATHERINE_E_TIMEOUT if no datagram arrived within the session's
 *   receive timeout, or -- on a pinned session, which delegates to
 *   recv_pinned() -- if its discard budget was spent.
 * \retval KATHERINE_E_INVAL if recvfrom(2) rejected an argument of the
 *   receive; see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel had no memory for the receive; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO for any other errno; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 */
static katherine_error_t
recv_datagram(katherine_udp_t *u, void *data, size_t count, size_t *received, int flags)
{
    if (u->remote_pinned) {
        return recv_pinned(u, data, count, received, flags);
    }

    ssize_t res = recv_one(u, data, count, &u->address_remote, flags);
    if (res == -1) {
        return recv_failure(u, errno);
    }

    *received = (size_t) res;
    return KATHERINE_E_OK;
}

/**
 * Initialize new UDP session.
 * \param u UDP session to initialize
 * \param local_port Local port number
 * \param remote_address Remote IP address
 * \param remote_port Remote port number
 * \param timeout_ms Communication timeout in milliseconds (zero if disabled)
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDRESS if remote_address is not a valid dotted-quad IPv4
 *   address, or local_port could not be bound on the wildcard address --
 *   another socket already holds it, or it is a privileged port; see
 *   inet_pton(3) and bind(2). Only the bind failure records an OS error for
 *   katherine_udp_last_os_error(); a rejected address string leaves it zero.
 * \retval KATHERINE_E_IO if the socket could not be created, or its address
 *   reuse or receive timeout option could not be set, for a reason none of
 *   the other codes cover -- the process being out of file descriptors, for
 *   one; see socket(2), setsockopt(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if creating the socket, setting its options, or
 *   initializing its mutex reported an invalid argument; see socket(2),
 *   setsockopt(2), pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if creating the socket, setting its options, or
 *   initializing its mutex ran out of memory; see socket(2), setsockopt(2),
 *   pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the session's mutex could not be initialized
 *   for a reason none of the other codes cover; see pthread_mutex_init(3)
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the session's mutex could not be
 *   initialized for lack of a non-memory system resource; see
 *   pthread_mutex_init(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_address, uint16_t remote_port, uint32_t timeout_ms)
{
    return katherine_udp_init_bound(u, NULL, local_port, remote_address, remote_port, timeout_ms);
}

/**
 * Initialize new UDP session, binding the local socket to a specific local address.
 *
 * This is the general form of katherine_udp_init(), which is a thin wrapper calling this function
 * with a NULL local address (i.e. the wildcard address). It is useful for hosts with several local
 * addresses (e.g. a daemon serving several emulated readouts, each bound to a distinct address on
 * the same port).
 *
 * \param u UDP session to initialize
 * \param local_address Local IP address to bind to, or NULL for the wildcard address (INADDR_ANY)
 * \param local_port Local port number
 * \param remote_address Remote IP address
 * \param remote_port Remote port number
 * \param timeout_ms Communication timeout in milliseconds (zero if disabled)
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDRESS if local_address (when not NULL) or remote_address is
 *   not a valid dotted-quad IPv4 address, or that local address and port
 *   could not be bound -- another socket already holds them, the address is
 *   not local to this host, or the port is privileged; see inet_pton(3) and
 *   bind(2). Only the bind failure records an OS error for
 *   katherine_udp_last_os_error(); a rejected address string leaves it zero.
 * \retval KATHERINE_E_IO if the socket could not be created, or its address
 *   reuse or receive timeout option could not be set, for a reason none of
 *   the other codes cover -- the process being out of file descriptors, for
 *   one; see socket(2), setsockopt(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if creating the socket, setting its options, or
 *   initializing its mutex reported an invalid argument; see socket(2),
 *   setsockopt(2), pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if creating the socket, setting its options, or
 *   initializing its mutex ran out of memory; see socket(2), setsockopt(2),
 *   pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the session's mutex could not be initialized
 *   for a reason none of the other codes cover; see pthread_mutex_init(3)
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the session's mutex could not be
 *   initialized for lack of a non-memory system resource; see
 *   pthread_mutex_init(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_init_bound(katherine_udp_t *u, const char *local_address, uint16_t local_port, const char *remote_address, uint16_t remote_port, uint32_t timeout_ms)
{
    katherine_error_t res = 0;

    // A session starts out tracking the sender of the datagram it last
    // received (see katherine_udp_pin_remote()) and tolerating the response
    // identifiers the readout firmware substitutes for its own (see
    // katherine_transport_set_strict_ack()). Callers hand this function
    // uninitialized storage, so the defaults cannot be left to the
    // allocation.
    u->remote_pinned     = false;
    u->count_drops       = false;
    u->last_drop_count   = 0;
    u->dropped_datagrams = 0;
    u->last_os_error     = 0;

    // Create socket.
    if ((u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        u->last_os_error = errno;
        res              = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_socket;
    }

    // Allow another local process (e.g. the ksim daemon) to reuse the same
    // port number; without this, a second bind() to 1555 or 1556 on this host
    // fails outright, whether its local address differs or is the very same
    // one -- measured both ways on glibc. Windows needs no such option, its
    // datagram sockets permitting that second bind by default, which is why
    // udp_win.c sets nothing here and says so.
    int reuseaddr = 1;
    if (setsockopt(u->sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        u->last_os_error = errno;
        res              = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_reuseaddr;
    }

    // Setup and bind the socket address.
    u->address_local.sin_family = AF_INET;
    u->address_local.sin_port   = htons(local_port);
    if (local_address == NULL) {
        u->address_local.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, local_address, &u->address_local.sin_addr) <= 0) {
        // inet_pton() rejecting the string is not an OS-level failure, so
        // last_os_error is left at the 0 it was reset to above.
        res = KATHERINE_E_ADDRESS;
        goto err_local_address;
    }

    if (bind(u->sock, (struct sockaddr *) &u->address_local, sizeof(u->address_local)) == -1) {
        u->last_os_error = errno;
        res              = KATHERINE_E_ADDRESS;
        goto err_bind;
    }

    // Read the port back: a local_port of 0 asks the kernel to pick one, and
    // without this the session would go on claiming the 0 it was asked for.
    socklen_t bound_len = sizeof(u->address_local);
    (void) getsockname(u->sock, (struct sockaddr *) &u->address_local, &bound_len);

    if (timeout_ms > 0) {
        if ((res = katherine_udp_set_timeout(u, timeout_ms)) != KATHERINE_E_OK) {
            goto err_timeout;
        }
    }

    {
        // Increase receive buffer size because the default one is too small to handle bursts of hits.
        //
        // Halving on refusal rather than failing the session: Linux clamps a
        // request above its rmem_max and reports success, while macOS refuses
        // one its kern.ipc.maxsockbuf cannot cover once mbuf overhead counts
        // against it -- and the stock ceiling there is the very size asked for,
        // so every session on such a host failed to open at all. A session that
        // cannot get even the smallest of these still fails, since a host that
        // constrained cannot hold a burst of hits whatever it is told.
        static const int wanted[] = {
            KATHERINE_UDP_RCVBUF_DEFAULT,
            KATHERINE_UDP_RCVBUF_DEFAULT / 2,
            KATHERINE_UDP_RCVBUF_DEFAULT / 4,
            KATHERINE_UDP_RCVBUF_DEFAULT / 8,
        };
        size_t i;
        int err = 0;

        for (i = 0; i < sizeof(wanted) / sizeof(*wanted); ++i) {
            if (setsockopt(u->sock, SOL_SOCKET, SO_RCVBUF, &wanted[i], sizeof(wanted[i])) == 0) {
                break;
            }
            err = errno;
        }

        // Kept out of the session until the last size has been refused too: a
        // session that opened reports no error, and a refusal this loop
        // recovered from is not one the caller can act on.
        if (i == sizeof(wanted) / sizeof(*wanted)) {
            u->last_os_error = err;
            res              = katherine_udp_map_socket_error(err, KATHERINE_E_IO);
            goto err_rcvbuf;
        }
    }

    // Set remote socket address.
    u->address_remote.sin_family = AF_INET;
    u->address_remote.sin_port   = htons(remote_port);
    if (remote_address == NULL) {
        u->last_os_error = 0;
        res              = KATHERINE_E_ADDRESS;
        goto err_remote;
    }

    if (inet_pton(AF_INET, remote_address, &u->address_remote.sin_addr) <= 0) {
        res = KATHERINE_E_ADDRESS;
        goto err_remote;
    }

    int mres = pthread_mutex_init(&u->mutex, NULL);
    if (mres != 0) {
        u->last_os_error = mres;
        res              = katherine_udp_map_socket_error(mres, KATHERINE_E_SYSTEM);
        goto err_mutex;
    }

    return KATHERINE_E_OK;

err_mutex:
err_remote:
err_rcvbuf:
err_timeout:
err_bind:
err_local_address:
err_reuseaddr:
    close(u->sock);
err_socket:
    // Left in the state katherine_udp_fini() reads as "nothing to release".
    // Without this the descriptor just closed stayed in the session, and
    // finalizing afterwards -- which an error path that does not know how far
    // setup got will do -- closed it a second time, by then very possibly
    // belonging to somebody else. The mutex is worse: on every path but
    // err_mutex it was never initialized at all.
    u->sock = 0;
    return res;
}

/**
 * Finalize UDP session.
 * \param u UDP session to finalize
 */
void
katherine_udp_fini(katherine_udp_t *u)
{
    // Guard against double-free. Otherwise this would close stdin (sock == 0).
    if (u->sock == 0) return;

    // Ignoring return codes below.
    (void) close(u->sock);
    (void) pthread_mutex_destroy(&u->mutex);

    u->sock = 0;
}

/**
 * Send a message (unreliable).
 * \param u UDP session
 * \param data Message start
 * \param count Message length in bytes
 *
 * \retval KATHERINE_E_OK on success, the whole message having been handed to
 *   the network stack.
 * \retval KATHERINE_E_TIMEOUT if the send would have blocked, which
 *   needs the caller to have made the session's socket non-blocking; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the message could not be handed to the network
 *   stack -- no route to the session's remote host, a datagram too large to
 *   send in one piece, or any other OS-level failure none of the other codes
 *   cover; see sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if sendto(2) rejected an argument of the send,
 *   as it does for a datagram addressed into a blackhole route; see ip(7)
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the network stack had no memory to queue the
 *   datagram; see sendto(2) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    ssize_t sent;
    size_t total      = 0;
    const char *cdata = (const char *) data;
    do {
        sent = sendto(u->sock, cdata + total, count - total, 0, (struct sockaddr *) &u->address_remote, sizeof(u->address_remote));
        if (sent == -1) {
            u->last_os_error = errno;
            return katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        }

        total += sent;
    } while (total < count);

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Sent:", data, count);
#endif /* KATHERINE_DEBUG_UDP */

    return KATHERINE_E_OK;
}

/**
 * Receive a message (unreliable).
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes
 *
 * \retval KATHERINE_E_OK on success, with count bytes in the buffer, taken
 *   from as many datagrams as it took to fill it.
 * \retval KATHERINE_E_TIMEOUT if a datagram of the message did not arrive
 *   within the session's receive timeout (the timeout_ms of
 *   katherine_udp_init(), which zero disables), or -- on a pinned session,
 *   see katherine_udp_pin_remote() -- if KATHERINE_UDP_PIN_MAX_DISCARDS
 *   datagrams from other hosts arrived in its place. Both are deliberately
 *   the same code, so no caller needs a separate path for either; the bytes
 *   received so far are left in the buffer, uncounted.
 * \retval KATHERINE_E_IO if a receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if recvfrom(2) rejected an argument of the
 *   receive; see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel had no memory for the receive; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
{
    size_t received = 0;
    size_t total    = 0;
    char *cdata     = (char *) data;

    // A pinned session verifies every datagram of the message separately, and
    // the discard budget is spent per datagram rather than per message.
    while (total < count) {
        katherine_error_t res = recv_datagram(u, cdata + total, count - total, &received, 0);
        if (res != 0) {
            return res;
        }

        total += received;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    return KATHERINE_E_OK;
}

/**
 * Receive a portion of a message (unreliable).
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes
 *
 * \retval KATHERINE_E_OK on success, with *count set to the bytes received.
 *   A datagram longer than the buffer fills it and the rest of that datagram
 *   is dropped: a full buffer, not a failure.
 * \retval KATHERINE_E_TIMEOUT if no datagram arrived within the session's
 *   receive timeout (the timeout_ms of katherine_udp_init(), which zero
 *   disables), or -- on a pinned session, see katherine_udp_pin_remote() --
 *   if KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts arrived
 *   instead. Both are deliberately the same code, so no caller needs a
 *   separate path for either.
 * \retval KATHERINE_E_IO if the receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if recvfrom(2) rejected an argument of the
 *   receive; see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel had no memory for the receive; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received       = 0;
    katherine_error_t res = recv_datagram(u, data, *count, &received, 0);

    if (res != 0) {
        return res;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    *count = received;
    return KATHERINE_E_OK;
}

/**
 * Receive a portion of a message without waiting for one.
 *
 * Identical to katherine_udp_recv(), except that a session with nothing
 * already queued reports KATHERINE_E_TIMEOUT at once instead of blocking
 * for its receive timeout. That is what makes it usable to flush a session
 * before a command exchange: the flush must cost nothing on the empty
 * socket it finds in the ordinary case, which every command of the library
 * would otherwise pay for.
 *
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes on entry, bytes received on
 *   success
 *
 * \retval KATHERINE_E_OK on success, with *count set to the bytes received.
 *   A datagram longer than the buffer fills it and the rest of that datagram
 *   is dropped: a full buffer, not a failure.
 * \retval KATHERINE_E_TIMEOUT if the socket had nothing queued, or -- on a
 *   pinned session, see katherine_udp_pin_remote() -- if what was queued
 *   came only from other hosts, whether the queue or the
 *   KATHERINE_UDP_PIN_MAX_DISCARDS discard budget ran out first. No case
 *   here waits, and each reports the code an expired receive timeout would,
 *   so no caller needs a separate path for any of them.
 * \retval KATHERINE_E_IO if the receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if recvfrom(2) rejected an argument of the
 *   receive; see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel had no memory for the receive; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received       = 0;
    katherine_error_t res = recv_datagram(u, data, *count, &received, MSG_DONTWAIT);

    if (res != 0) {
        return res;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    *count = received;
    return KATHERINE_E_OK;
}

/**
 * Repoint the remote address of a UDP session.
 *
 * On an unpinned session, katherine_udp_recv() and katherine_udp_recv_exact() already update the
 * remote address to whoever last sent to it, which is how a server naturally replies to its last
 * peer. This function instead sets the *initial or overriding* destination used for outgoing
 * messages until the next inbound datagram arrives (or until this function is called again) --
 * useful for a session that only ever sends, such as a data-only socket that must be redirected to
 * a peer learned over a different session. On a pinned session (see katherine_udp_pin_remote())
 * this function is the only way the remote address ever moves, and the pin follows it: from here on
 * the newly named host is the one whose datagrams the session accepts.
 *
 * \param u UDP session
 * \param remote_address Remote IP address
 * \param remote_port Remote port number
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDRESS if remote_address is not a valid dotted-quad IPv4
 *   address, in which case the session keeps the remote address it had; see
 *   inet_pton(3). No system call fails here, so
 *   katherine_udp_last_os_error() is left reporting zero.
 */
katherine_error_t
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_address, uint16_t remote_port)
{
    if (remote_address == NULL) {
        u->last_os_error = 0;
        return KATHERINE_E_ADDRESS;
    }

    struct sockaddr_in address_remote;
    address_remote.sin_family = AF_INET;
    address_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_address, &address_remote.sin_addr) <= 0) {
        // Same address-resolution failure as katherine_udp_init_bound(), and
        // not an OS-level one either.
        u->last_os_error = 0;
        return KATHERINE_E_ADDRESS;
    }

    u->address_remote = address_remote;
    return KATHERINE_E_OK;
}

/**
 * Pin the remote address of a UDP session, opting it out of tracking the sender.
 *
 * A session starts out unpinned, where every received datagram makes its sender the session's
 * remote address: the behavior a server wants, and a hazard for a client, whose session a single
 * stray datagram -- a late response, a scan, a datagram delivered back to its own sender -- then
 * retargets for good (issue #23, "net: stop stray datagrams retargeting remote addr"). A pinned
 * session keeps sending where it was told to by katherine_udp_init() or
 * katherine_udp_set_remote(), and silently discards inbound datagrams from any other host, up to
 * KATHERINE_UDP_PIN_MAX_DISCARDS of them per receive call before reporting
 * KATHERINE_E_TIMEOUT.
 *
 * Only the remote host is pinned, not its port, because a readout answers commands from its
 * command port but streams measurement data from another. Pinning cannot be undone.
 *
 * \param u UDP session
 */
void
katherine_udp_pin_remote(katherine_udp_t *u)
{
    u->remote_pinned = true;
}

/**
 * Lock mutual exclusion synchronization primitive.
 * \param u UDP session
 *
 * \retval KATHERINE_E_OK on success, with the session's lock held by the
 *   calling thread.
 * \retval KATHERINE_E_INVAL if the session's mutex was not in a lockable
 *   state -- a session katherine_udp_init() never initialized, or one
 *   already finalized by katherine_udp_fini(); see pthread_mutex_lock(3)
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the lock could not be taken for a reason
 *   none of the other codes cover; see pthread_mutex_lock(3) and
 *   katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT.
 *   pthread_mutex_lock(3) documents EBUSY, EDEADLK, EINVAL and EPERM and no
 *   more, so neither code can arrive however the map is capable of producing
 *   them.
 */
katherine_error_t
katherine_udp_mutex_lock(katherine_udp_t *u)
{
    katherine_error_t err = pthread_mutex_lock(&u->mutex);
    if (err != 0) {
        u->last_os_error = err;
        return katherine_udp_map_socket_error(err, KATHERINE_E_SYSTEM);
    }

    return KATHERINE_E_OK;
}

/**
 * Unlock mutual exclusion synchronization primitive.
 * \param u UDP session
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the session's mutex was not in an unlockable
 *   state -- a session katherine_udp_init() never initialized, or one
 *   already finalized by katherine_udp_fini(); see pthread_mutex_unlock(3)
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the lock could not be released for a reason
 *   none of the other codes cover, such as this thread not being the one
 *   holding it; see pthread_mutex_unlock(3) and
 *   katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   for the same reason as the lock above.
 */
katherine_error_t
katherine_udp_mutex_unlock(katherine_udp_t *u)
{
    katherine_error_t err = pthread_mutex_unlock(&u->mutex);
    if (err != 0) {
        u->last_os_error = err;
        return katherine_udp_map_socket_error(err, KATHERINE_E_SYSTEM);
    }

    return KATHERINE_E_OK;
}

/**
 * Read the OS-level detail of a UDP session's most recent transport failure.
 * \param u UDP session
 * \return The raw OS error code behind the session's last failure, or 0 if
 *   it succeeded, or failed without one (e.g. a malformed address argument).
 */
int
katherine_udp_last_os_error(const katherine_udp_t *u)
{
    return u->last_os_error;
}

/**
 * Set how long a receive on a session waits before reporting
 * KATHERINE_E_TIMEOUT.
 *
 * Separate from the timeout katherine_udp_init() applies, so that a caller
 * which needs one limit for a slow exchange and another for a fast one does
 * not have to reopen the session.
 *
 * \param u UDP session
 * \param timeout_ms Limit on one receive, in milliseconds; zero removes the
 *   limit, leaving a receive to block until something arrives
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the option or its size was rejected; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the request failed at the OS level for a reason
 *   none of the other codes cover, or the handle is not an open socket; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   which reach this list only through the shared error map; setsockopt(2)
 *   documents neither.
 */
katherine_error_t
katherine_udp_set_timeout(katherine_udp_t *u, uint32_t timeout_ms)
{
    katherine_error_t res = 0;

    struct timeval timeout;
    timeout.tv_sec  = timeout_ms / 1000;
    timeout.tv_usec = 1000 * (timeout_ms % 1000);

    if (setsockopt(u->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        u->last_os_error = errno;
        res              = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_setsockopt;
    }

    return KATHERINE_E_OK;

err_setsockopt:
    return res;
}

/**
 * Choose how much the OS queues for a session before it discards a datagram
 * that arrived and has not been read yet.
 *
 * Sessions open with as much of KATHERINE_UDP_RCVBUF_DEFAULT as the host
 * grants; this is for a caller who knows their data rate, or their stalls,
 * better. Unlike opening a session, this reports a refusal rather than asking
 * for less: a caller naming a size is answered about that size.
 * Raising it after an acquisition has begun is allowed and takes effect for
 * datagrams not yet queued, but the datagrams already lost cannot be asked for
 * again, so a caller sizing for a known burst should do it beforehand.
 *
 * The request is advisory in two directions and succeeds either way, which is
 * what katherine_udp_rcvbuf() is for: the OS may grant less, having its own
 * administrative ceiling (net.core.rmem_max on Linux), and it may account the
 * total differently from the payload it holds.
 *
 * \param u UDP session
 * \param bytes Requested queue size; 0 asks for the OS minimum rather than
 *   leaving the size alone
 *
 * \retval KATHERINE_E_OK on success, including when the OS granted less than
 *   was asked for.
 * \retval KATHERINE_E_INVAL if the option or its size was rejected; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the request failed at the OS level for a reason
 *   none of the other codes cover, or the handle is not an open socket; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   which reach this list only through the shared error map; setsockopt(2)
 *   documents neither.
 */
katherine_error_t
katherine_udp_set_rcvbuf(katherine_udp_t *u, uint32_t bytes)
{
    katherine_error_t res = 0;

    // Signed, and narrower than the argument: the option is an int either way,
    // so a request above INT_MAX is capped rather than wrapped to a negative
    // one, which the OS would refuse for the wrong reason.
    const int want = (bytes > (uint32_t) INT_MAX) ? INT_MAX : (int) bytes;

    if (setsockopt(u->sock, SOL_SOCKET, SO_RCVBUF, &want, sizeof(want)) == -1) {
        u->last_os_error = errno;
        res              = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_setsockopt;
    }

    return KATHERINE_E_OK;

err_setsockopt:
    return res;
}

/**
 * Read how much the OS is actually queueing for a session.
 *
 * Worth reading rather than assuming, since neither the default nor a request
 * through katherine_udp_set_rcvbuf() is necessarily what was granted: the OS
 * clamps silently to its own ceiling. This is how a caller learns it got less
 * than it asked for, the only other symptom being lost measurement data.
 *
 * The number is the OS's own accounting, not a payload capacity. Linux reports
 * twice what was asked and charges per-datagram overhead against the total, so
 * the payload that actually fits comes out near the value requested; measured
 * on a Gen2 stream of 60 690 B datagrams, about 57% of the reported size.
 *
 * \param u UDP session
 * \param bytes Where to store the size, in bytes
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if bytes is NULL, or the option or its size was
 *   rejected; see getsockopt(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the query failed at the OS level for a reason
 *   none of the other codes cover, or the handle is not an open socket; see
 *   getsockopt(2) and katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   which reach this list only through the shared error map; getsockopt(2)
 *   documents neither.
 */
katherine_error_t
katherine_udp_rcvbuf(const katherine_udp_t *u, uint32_t *bytes)
{
    katherine_error_t res = 0;

    if (bytes == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_bytes_null;
    }

    int got       = 0;
    socklen_t len = sizeof(got);
    if (getsockopt(u->sock, SOL_SOCKET, SO_RCVBUF, &got, &len) == -1) {
        res = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_getsockopt;
    }

    // Negative is not a size the OS is documented to report, and a caller
    // comparing an unsigned reading against its request must not be handed one.
    *bytes = (got < 0) ? 0 : (uint32_t) got;

    return KATHERINE_E_OK;

err_getsockopt:
err_bytes_null:
    return res;
}

/**
 * Ask the kernel to report the datagrams it drops for want of queue space.
 *
 * Nothing in the protocol numbers datagrams, so a measurement stream that
 * overruns its receive queue loses hits with no trace: the readout's own
 * counters see nothing, the decoder sees a shorter stream, and the only
 * symptom is a frame whose received pixel count falls short of the count the
 * readout says it sent. This counter is the one place that loss is visible.
 *
 * Off by default, and the receive path pays nothing for it while it is: a
 * counting session receives through recvmsg(2) and walks one control message
 * per datagram, which the data path takes millions of.
 *
 * The kernel reports its count alongside a datagram it did deliver, so the
 * count only advances while datagrams arrive: a session whose queue overran
 * and then went quiet learns what it lost on its next successful receive,
 * and never if there is none. Read it after a frame rather than expecting it
 * to be current at the moment of the loss.
 *
 * \note Linux only. The option does not exist elsewhere, and where it does
 *   not this call reports KATHERINE_E_UNSUPPORTED rather than pretending to
 *   count.
 *
 * Either direction zeroes the count, so a caller reads what was dropped since
 * it last asked rather than a running total it has already seen. What is not
 * reset is the baseline the deltas are taken against: the kernel's counter
 * runs whether or not the option is set, and clearing the baseline would make
 * the next datagram report the session's whole history over again. Drops
 * suffered while counting was off are therefore added to the first count
 * after it is switched back on -- they did happen to this session.
 *
 * \param u Session to change.
 * \param enabled True to start counting, false to stop and reset.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_UNSUPPORTED if the platform has no such option.
 * \retval KATHERINE_E_INVAL if setsockopt(2) rejected the request; see
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if setsockopt(2) failed for any other reason; see
 *   katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   which reach this list only through the shared error map; setsockopt(2)
 *   documents neither.
 * \note The indexer cannot follow a branch the preprocessor removed:
 *   KATHERINE_E_UNSUPPORTED, which is what a platform without the option
 *   answers.
 */
katherine_error_t
katherine_udp_set_drop_counting(katherine_udp_t *u, bool enabled)
{
#ifdef SO_RXQ_OVFL
    const int on = enabled ? 1 : 0;

    if (setsockopt(u->sock, SOL_SOCKET, SO_RXQ_OVFL, &on, sizeof(on)) != 0) {
        u->last_os_error = errno;
        return katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
    }

    u->count_drops       = enabled;
    u->dropped_datagrams = 0;
    return KATHERINE_E_OK;
#else
    (void) enabled;
    u->count_drops       = false;
    u->dropped_datagrams = 0;
    return KATHERINE_E_UNSUPPORTED;
#endif /* SO_RXQ_OVFL */
}

/**
 * Datagrams the kernel has dropped on this session for want of queue space.
 *
 * Counts only what fell while katherine_udp_set_drop_counting() was on, and only
 * what the kernel attributes to a full queue -- a datagram lost on the wire
 * or dropped by a switch is nobody's to count.
 *
 * \param u Session to ask.
 * \return The count, or zero on a session that was never counting.
 */
uint64_t
katherine_udp_get_dropped_datagrams(const katherine_udp_t *u)
{
    return u->dropped_datagrams;
}

/**
 * Allow this session to send to a broadcast address.
 *
 * Off by default, as the OS has it: a socket that has not asked refuses a
 * send to a broadcast address rather than delivering it to one host. Asked
 * for by katherine_device_discover_udp(), which is the only thing in this library
 * needs it.
 *
 * \param u UDP session
 * \param enabled True to allow broadcast sends, false to refuse them again
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the option or its size was rejected; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the request failed at the OS level for a reason
 *   none of the other codes cover, or the handle is not an open socket; see
 *   setsockopt(2) and katherine_udp_last_os_error().
 *
 * \note Unreachable here: KATHERINE_E_NOMEM and KATHERINE_E_TIMEOUT,
 *   which reach this list only through the shared error map; setsockopt(2)
 *   documents neither.
 */
katherine_error_t
katherine_udp_set_broadcast(katherine_udp_t *u, bool enabled)
{
    katherine_error_t res;
    const int on = enabled ? 1 : 0;

    if (setsockopt(u->sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) == -1) {
        u->last_os_error = errno;
        res              = katherine_udp_map_socket_error(errno, KATHERINE_E_IO);
        goto err_setsockopt;
    }

    return KATHERINE_E_OK;

err_setsockopt:
    return res;
}

/**
 * The address this session currently sends to, as a dotted quad.
 *
 * On an unpinned session that is whoever answered last, which is what makes
 * it worth reading: a receive adopts the sender, so this says where the
 * datagram just taken came from. A pinned session reports the address it was
 * pinned to and nothing else.
 *
 * \param u UDP session
 * \param address Destination for the address, KATHERINE_DEVICE_ADDRESS_STR_SIZE bytes or more
 * \param address_size Size of that destination
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the buffer is too small for an address, or if
 *   the conversion rejected its arguments; see inet_ntop(3).
 */
katherine_error_t
katherine_udp_get_remote_address(const katherine_udp_t *u, char *address, size_t address_size)
{
    katherine_error_t res;

    if (address_size < KATHERINE_DEVICE_ADDRESS_STR_SIZE) {
        res = KATHERINE_E_INVAL;
        goto err_address_size;
    }

    if (inet_ntop(AF_INET, &u->address_remote.sin_addr, address, (socklen_t) address_size) == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_convert;
    }

    return KATHERINE_E_OK;

err_convert:
err_address_size:
    return res;
}

#endif /* KATHERINE_FEATURE_UDP && KATHERINE_NIX */
