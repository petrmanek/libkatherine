/**
 * \file
 * \brief Win32 implementation of the UDP communication layer.
 * \author Felix Lehner
 * \date 31.8.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/global.h>

void
empty_method(void)
{ }

#ifdef KATHERINE_WIN

#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <katherine/error.h>
#include <katherine/udp.h>

#include "transport/udp_error_map.h"

#ifdef KATHERINE_DEBUG_UDP
static inline void
dump_buffer(const char *msg, const unsigned char *buf, size_t count)
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

// True if a datagram received from addr counts as coming from the pinned
// remote of session u.
//
// Only the host is compared, never the port: a readout answers commands from
// its command port but streams measurement data from another one (1556 vs
// 1555 for the emulated readout), and no source port of the firmware is
// specified anywhere, whereas the hazard a pin guards against -- another
// peer's stray datagram becoming the session's remote -- is a property of
// the host.
static bool
from_pinned_remote(const katherine_udp_t *u, const SOCKADDR_IN *addr)
{
    return addr->sin_addr.s_addr == u->addr_remote.sin_addr.s_addr;
}

/**
 * Reports a failed receive, recording the OS-level detail only when there is
 * any: an expired receive timeout is an ordinary outcome rather than a fault,
 * so it leaves no Winsock code behind for katherine_udp_last_os_error() to
 * hand out. The readiness check below clears it for the same reason.
 *
 * Deliberately duplicated from udp_nix.c rather than shared. The bodies are
 * identical, but nothing else these two files have in common is -- the two
 * receive loops differ substantially and from_pinned_remote() spells its
 * argument type differently -- so a header for one small function would earn
 * less than it costs, and each copy gets to name its own platform's codes.
 *
 * \param u UDP session
 * \param err Raw Winsock code of the failed call, from WSAGetLastError()
 *
 * \retval KATHERINE_E_TIMEOUT if err says the receive found nothing --
 *   WSAETIMEDOUT, the expired SO_RCVTIMEO of an idle socket, or
 *   WSAEWOULDBLOCK. This is the case that leaves last_os_error cleared.
 * \retval KATHERINE_E_INVAL if err is WSAEINVAL; see recvfrom().
 * \retval KATHERINE_E_NOMEM if err is WSAENOBUFS, the condition POSIX reports
 *   as ENOMEM; see recvfrom().
 * \retval KATHERINE_E_IO for any other Winsock code, this function's
 *   fallback -- WSAEINTR, say, where the receive was interrupted.
 * \retval KATHERINE_E_OK never: this is a failure path, and err is the code of
 *   a call that has already failed.
 */
static katherine_error_t
recv_failure(katherine_udp_t *u, int err)
{
    katherine_error_t mapped = katherine_udp_map_socket_error(err, KATHERINE_E_IO);

    u->last_os_error = (mapped == KATHERINE_E_TIMEOUT) ? 0 : err;
    return mapped;
}

// Reports whether a datagram is already queued on the socket of session u,
// without waiting for one to arrive: 0 if the recvfrom() that follows will
// not block, KATHERINE_E_TIMEOUT if the socket is empty -- the same code the
// EAGAIN of a MSG_DONTWAIT receive maps to on POSIX.
//
// Winsock has no MSG_DONTWAIT, and switching the socket to non-blocking mode
// around a receive would disturb the SO_RCVTIMEO every other call of this
// file relies on, so the queue is inspected instead. FIONREAD in preference
// to a zero-timeout select(): one call, no descriptor set, and no dependency
// on the loop the platform's FD_SET macro expands to.
//
// One blind spot follows from asking for a byte count: a zero-length
// datagram is indistinguishable from an empty socket, so it is not drained
// here. Only katherine_cmd_drain() reaches this path, and it is best-effort
// by contract -- a datagram it leaves behind is read by the correlation that
// follows, which rejects anything that is not a whole response. The readout
// sends no empty datagrams at all.
static katherine_error_t
recv_ready(katherine_udp_t *u)
{
    u_long available = 0;

    if (ioctlsocket(u->sock, FIONREAD, &available) == SOCKET_ERROR) {
        u->last_os_error = WSAGetLastError();
        return katherine_udp_map_socket_error(u->last_os_error, KATHERINE_E_IO);
    }

    if (available == 0) {
        u->last_os_error = 0;
        return KATHERINE_E_TIMEOUT;
    }

    return KATHERINE_E_OK;
}

// Receives one datagram from the pinned remote of session u, discarding up to
// KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts on the way there.
// Spending that budget is reported as KATHERINE_E_TIMEOUT, the very code
// the expired receive timeout of an idle socket yields, so that no caller
// needs a separate path for it.
//
// With nowait set, every receive is preceded by the queue check of
// recv_ready() above, so the call never blocks; that is how
// katherine_udp_recv_nowait() reaches this loop.
//
// The pinned address is read from addr_remote itself rather than from a copy
// taken when the pin was placed, which is what makes the pin follow
// katherine_udp_set_remote().
static katherine_error_t
recv_pinned(katherine_udp_t *u, void *data, size_t count, size_t *received, bool nowait)
{
    char *cdata = (char *) data;

    for (uint32_t discarded = 0; discarded < KATHERINE_UDP_PIN_MAX_DISCARDS; ++discarded) {
        SOCKADDR_IN addr_from;
        socklen_t addr_len = sizeof(addr_from);

        if (nowait) {
            katherine_error_t ready = recv_ready(u);
            if (ready != 0) return ready;
        }

        int res = recvfrom(u->sock, cdata, (int) count, 0, (struct sockaddr *) &addr_from, &addr_len);
        if (res == SOCKET_ERROR) {
            // A datagram larger than the buffer fails with WSAEMSGSIZE here,
            // after being consumed and with the source address filled in,
            // where POSIX instead truncates it silently and reports the
            // buffer-full length. One from a foreign host is a discard like
            // any other; one from the pinned remote is reported the way
            // POSIX reports it, as a full buffer, so that a caller sizing a
            // buffer to detect an overlong datagram -- as the command
            // response path does -- sees the same thing on both platforms.
            if (WSAGetLastError() == WSAEMSGSIZE) {
                if (!from_pinned_remote(u, &addr_from)) continue;

                *received = count;
                return KATHERINE_E_OK;
            }

            return recv_failure(u, WSAGetLastError());
        }

        if (from_pinned_remote(u, &addr_from)) {
            *received = (size_t) res;
            return KATHERINE_E_OK;
        }
    }

    // The discard budget is spent, not an OS-level failure -- reported
    // exactly like an expired receive timeout (see KATHERINE_UDP_PIN_MAX_DISCARDS,
    // katherine/udp.h), so no caller needs a separate path for it.
    u->last_os_error = 0;
    return KATHERINE_E_TIMEOUT;
}

// Receives one datagram into data, honoring the pin of session u: a pinned
// session accepts only datagrams from its remote host and leaves addr_remote
// alone, an unpinned one accepts the next datagram from anybody and adopts
// its sender as the remote -- the server behavior of replying to whoever
// asked last.
static katherine_error_t
recv_datagram(katherine_udp_t *u, void *data, size_t count, size_t *received, bool nowait)
{
    char *cdata = (char *) data;

    if (u->remote_pinned) {
        return recv_pinned(u, cdata, count, received, nowait);
    }

    if (nowait) {
        katherine_error_t ready = recv_ready(u);
        if (ready != 0) return ready;
    }

    socklen_t addr_len = sizeof(u->addr_remote);
    int res            = recvfrom(u->sock, cdata, (int) count, 0, (struct sockaddr *) &u->addr_remote, &addr_len);
    if (res == SOCKET_ERROR) {
        // Truncation is not a failure here either, for the same reason as in
        // recv_pinned() above: the datagram is consumed and the buffer full,
        // which is exactly what POSIX reports for the same wire event.
        if (WSAGetLastError() == WSAEMSGSIZE) {
            *received = count;
            return KATHERINE_E_OK;
        }

        return recv_failure(u, WSAGetLastError());
    }

    *received = (size_t) res;
    return KATHERINE_E_OK;
}

/**
 * Initialize new UDP session.
 * \param u UDP session to initialize
 * \param local_port Local port number
 * \param remote_addr Remote IP address
 * \param remote_port Remote port number
 * \param timeout_ms Communication timeout in milliseconds (zero if disabled)
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDR if remote_addr is not a valid dotted-quad IPv4
 *   address, or local_port could not be bound on the wildcard address --
 *   another socket already holds it, or it is excluded from use; see
 *   inet_pton() and bind(). Only the bind failure records an OS error for
 *   katherine_udp_last_os_error(); a rejected address string leaves it zero.
 * \retval KATHERINE_E_IO if the socket could not be created, or -- when
 *   timeout_ms is nonzero -- its receive timeout could not be set, for a
 *   reason none of the other codes cover; see socket(), setsockopt() and
 *   katherine_udp_last_os_error(), which reports the WSAGetLastError() code.
 * \retval KATHERINE_E_INVAL if creating the socket or setting its receive
 *   timeout reported WSAEINVAL; see socket(), setsockopt() and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if creating the socket or setting its receive
 *   timeout found no buffer space (WSAENOBUFS), the condition POSIX reports
 *   as ENOMEM; see socket(), setsockopt() and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if Winsock could not be started for this
 *   process, or the session's mutex could not be created; see WSAStartup(),
 *   CreateMutex() and katherine_udp_last_os_error(), which reports
 *   WSAStartup()'s own return value in the first case and the
 *   GetLastError() code in the second.
 */
katherine_error_t
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    return katherine_udp_init_bound(u, NULL, local_port, remote_addr, remote_port, timeout_ms);
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
 * \param local_addr Local IP address to bind to, or NULL for the wildcard address (INADDR_ANY)
 * \param local_port Local port number
 * \param remote_addr Remote IP address
 * \param remote_port Remote port number
 * \param timeout_ms Communication timeout in milliseconds (zero if disabled)
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDR if local_addr (when not NULL) or remote_addr is
 *   not a valid dotted-quad IPv4 address, or that local address and port
 *   could not be bound -- another socket already holds them, the address is
 *   not local to this host, or the port is excluded from use; see
 *   inet_pton() and bind(). Only the bind failure records an OS error for
 *   katherine_udp_last_os_error(); a rejected address string leaves it zero.
 * \retval KATHERINE_E_IO if the socket could not be created, or -- when
 *   timeout_ms is nonzero -- its receive timeout could not be set, for a
 *   reason none of the other codes cover; see socket(), setsockopt() and
 *   katherine_udp_last_os_error(), which reports the WSAGetLastError() code.
 * \retval KATHERINE_E_INVAL if creating the socket or setting its receive
 *   timeout reported WSAEINVAL; see socket(), setsockopt() and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if creating the socket or setting its receive
 *   timeout found no buffer space (WSAENOBUFS), the condition POSIX reports
 *   as ENOMEM; see socket(), setsockopt() and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if Winsock could not be started for this
 *   process, or the session's mutex could not be created; see WSAStartup(),
 *   CreateMutex() and katherine_udp_last_os_error(), which reports
 *   WSAStartup()'s own return value in the first case and the
 *   GetLastError() code in the second.
 */
katherine_error_t
katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    katherine_error_t res = 0;

    // A session starts out tracking the sender of the datagram it last
    // received (see katherine_udp_pin_remote()) and tolerating the response
    // identifiers the readout firmware substitutes for its own (see
    // katherine_udp_set_strict_ack()). Callers hand this function
    // uninitialized storage, so the defaults cannot be left to the
    // allocation.
    u->remote_pinned           = false;
    u->strict_ack              = false;
    u->stray_command_responses = 0;
    u->last_os_error           = 0;

    // Create communication buffer.
    int wres = WSAStartup(MAKEWORD(2, 2), &u->wsa_data);
    if (wres != 0) {
        // WSAStartup() reports its own failure via its return value, not
        // WSAGetLastError().
        u->last_os_error = wres;
        res              = katherine_udp_map_socket_error(wres, KATHERINE_E_SYSTEM);
        goto err_wsa_data;
    }

    // Create socket.
    if ((u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        u->last_os_error = WSAGetLastError();
        res              = katherine_udp_map_socket_error(u->last_os_error, KATHERINE_E_IO);
        goto err_socket;
    }

    // Setup and bind the socket address.
    u->addr_local.sin_family = AF_INET;
    u->addr_local.sin_port   = htons(local_port);
    if (local_addr == NULL) {
        u->addr_local.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, local_addr, &u->addr_local.sin_addr) <= 0) {
        // inet_pton() rejecting the string is not an OS-level failure, so
        // last_os_error is left at the 0 it was reset to above.
        res = KATHERINE_E_ADDR;
        goto err_local_addr;
    }

    if (bind(u->sock, (const struct sockaddr *) &u->addr_local, sizeof(u->addr_local)) == SOCKET_ERROR) {
        u->last_os_error = WSAGetLastError();
        res              = KATHERINE_E_ADDR;
        goto err_bind;
    }

    if (timeout_ms > 0) {
        // Set socket timeout.
        DWORD timeout = timeout_ms;
        if (setsockopt(u->sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) == SOCKET_ERROR) {
            u->last_os_error = WSAGetLastError();
            res              = katherine_udp_map_socket_error(u->last_os_error, KATHERINE_E_IO);
            goto err_timeout;
        }
    }

    // Set remote socket address.
    u->addr_remote.sin_family = AF_INET;
    u->addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &u->addr_remote.sin_addr) <= 0) {
        res = KATHERINE_E_ADDR;
        goto err_remote;
    }

    if ((u->mutex = CreateMutex(NULL, FALSE, NULL)) == NULL) {
        // Reported without a mapping: GetLastError() answers in the Win32
        // system-error space, not the Winsock one that
        // katherine_udp_map_socket_error() reads, and the codes CreateMutex()
        // documents have no counterpart in this library's domain beyond the
        // group named here. The raw code is preserved for a caller that wants
        // the detail.
        u->last_os_error = (int) GetLastError();
        res              = KATHERINE_E_SYSTEM;
        goto err_mutex;
    }

    return KATHERINE_E_OK;

err_mutex:
err_remote:
err_timeout:
err_bind:
err_local_addr:
    (void) closesocket(u->sock);
err_socket:
    (void) WSACleanup();
err_wsa_data:
    return res;
}

/**
 * Finalize UDP session.
 * \param u UDP session to finalize
 */
void
katherine_udp_fini(katherine_udp_t *u)
{
    // Ignoring return codes below.
    (void) closesocket(u->sock);
    (void) CloseHandle(u->mutex);
    (void) WSACleanup();
}

/**
 * Send a message (unreliable).
 * \param u UDP session
 * \param data Message start
 * \param count Message length in bytes
 *
 * \retval KATHERINE_E_OK on success, the whole message having been handed to
 *   the network stack.
 * \retval KATHERINE_E_IO if the message could not be handed to the network
 *   stack for a reason none of the other codes cover -- no route to the
 *   session's remote host, or a datagram too large to send in one piece; see
 *   sendto() and katherine_udp_last_os_error(), which reports the
 *   WSAGetLastError() code itself.
 * \retval KATHERINE_E_INVAL if sendto() rejected an argument of the send
 *   (WSAEINVAL); see sendto() and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the network stack had no buffer space to queue
 *   the datagram (WSAENOBUFS), the condition POSIX reports as ENOMEM; see
 *   sendto() and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    size_t sent;
    size_t total      = 0;
    const char *cdata = (const char *) data;

    do {
        sent = sendto(u->sock, cdata + total, (int) (count - total), 0, (struct sockaddr *) &u->addr_remote, sizeof(u->addr_remote));
        if (sent == SOCKET_ERROR) {
            u->last_os_error = WSAGetLastError();
            return katherine_udp_map_socket_error(u->last_os_error, KATHERINE_E_IO);
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
 *   from as many datagrams as it took to fill it. A datagram longer than the
 *   space left fills it and the rest of that datagram is dropped: Winsock
 *   fails such a receive with WSAEMSGSIZE after consuming the datagram, and
 *   this transport reports it back as the full buffer POSIX reports, not as
 *   an error.
 * \retval KATHERINE_E_TIMEOUT if a datagram of the message did not arrive
 *   within the session's receive timeout (the timeout_ms of
 *   katherine_udp_init(), which zero disables), or -- on a pinned session,
 *   see katherine_udp_pin_remote() -- if KATHERINE_UDP_PIN_MAX_DISCARDS
 *   datagrams from other hosts arrived in its place. Both are deliberately
 *   the same code, so no caller needs a separate path for either; the bytes
 *   received so far are left in the buffer, uncounted. An expired timeout
 *   and a spent discard budget both leave katherine_udp_last_os_error()
 *   reporting zero, there being no OS-level fault behind either.
 * \retval KATHERINE_E_IO if a receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom() and
 *   katherine_udp_last_os_error(), which reports the WSAGetLastError()
 *   code -- WSAEINTR, say, where the receive was interrupted.
 * \retval KATHERINE_E_INVAL if recvfrom() rejected an argument of the receive
 *   (WSAEINVAL); see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the receive found no buffer space
 *   (WSAENOBUFS), the condition POSIX reports as ENOMEM; see recvfrom() and
 *   katherine_udp_last_os_error().
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
        katherine_error_t res = recv_datagram(u, cdata + total, count - total, &received, false);
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
 *   is dropped: Winsock fails such a receive with WSAEMSGSIZE after
 *   consuming the datagram, and this transport reports it back as the full
 *   buffer POSIX reports, not as an error.
 * \retval KATHERINE_E_TIMEOUT if no datagram arrived within the session's
 *   receive timeout (the timeout_ms of katherine_udp_init(), which zero
 *   disables), or -- on a pinned session, see katherine_udp_pin_remote() --
 *   if KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts arrived
 *   instead. Both are deliberately the same code, so no caller needs a
 *   separate path for either. An expired timeout and a
 *   spent discard budget both leave katherine_udp_last_os_error() reporting
 *   zero, there being no OS-level fault behind either.
 * \retval KATHERINE_E_IO if the receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom() and
 *   katherine_udp_last_os_error(), which reports the WSAGetLastError()
 *   code -- WSAEINTR, say, where the receive was interrupted.
 * \retval KATHERINE_E_INVAL if recvfrom() rejected an argument of the receive
 *   (WSAEINVAL); see katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the receive found no buffer space
 *   (WSAENOBUFS), the condition POSIX reports as ENOMEM; see recvfrom() and
 *   katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received;
    katherine_error_t res = recv_datagram(u, data, *count, &received, false);

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
 *   is dropped: Winsock fails such a receive with WSAEMSGSIZE after
 *   consuming the datagram, and this transport reports it back as the full
 *   buffer POSIX reports, not as an error.
 * \retval KATHERINE_E_TIMEOUT if the socket had no bytes queued, or -- on a
 *   pinned session, see katherine_udp_pin_remote() -- if what was queued
 *   came only from other hosts, whether the queue or the
 *   KATHERINE_UDP_PIN_MAX_DISCARDS discard budget ran out first. No case
 *   here waits, and each reports the code an expired receive timeout would,
 *   so no caller needs a separate path for any of them; the readiness check
 *   and the discard budget both clear katherine_udp_last_os_error() to zero.
 *   Winsock has no MSG_DONTWAIT, so readiness is decided by the byte count
 *   ioctlsocket() reports for FIONREAD, and a queued zero-length datagram is
 *   therefore indistinguishable from an empty socket: this code means
 *   nothing readable was found, not that the socket was left empty. The
 *   readout sends no empty datagrams, and the library's own use of this
 *   function -- the pre-command flush of katherine_cmd_drain() -- is
 *   best-effort by contract.
 * \retval KATHERINE_E_IO if the readiness check or the receive failed at the
 *   OS level for a reason none of the other codes cover; see ioctlsocket(),
 *   recvfrom() and katherine_udp_last_os_error(), which reports the
 *   WSAGetLastError() code -- WSAEINTR, say, where the receive was
 *   interrupted.
 * \retval KATHERINE_E_INVAL if the readiness check or the receive rejected an
 *   argument (WSAEINVAL); see ioctlsocket(), recvfrom() and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if either found no buffer space (WSAENOBUFS),
 *   the condition POSIX reports as ENOMEM; see ioctlsocket(), recvfrom() and
 *   katherine_udp_last_os_error().
 */
katherine_error_t
katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received;
    katherine_error_t res = recv_datagram(u, data, *count, &received, true);

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
 * \param remote_addr Remote IP address
 * \param remote_port Remote port number
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDR if remote_addr is not a valid dotted-quad IPv4
 *   address, in which case the session keeps the remote address it had; see
 *   inet_pton(). No system call fails here, so
 *   katherine_udp_last_os_error() is left reporting zero.
 */
katherine_error_t
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
{
    SOCKADDR_IN addr_remote;
    addr_remote.sin_family = AF_INET;
    addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &addr_remote.sin_addr) <= 0) {
        // Same address-resolution failure as katherine_udp_init_bound(), and
        // not an OS-level one either.
        u->last_os_error = 0;
        return KATHERINE_E_ADDR;
    }

    u->addr_remote = addr_remote;
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
 * Require command responses to repeat the operation code of their request exactly.
 *
 * A session correlates every command response it receives with the request in flight, by the
 * response identifier the readout puts in byte 6 of the eight-byte datagram, and discards -- and
 * counts, in katherine_udp_t::stray_command_responses -- whatever belongs to no request of its own.
 *
 * By default the correlation also accepts the identifiers the readout firmware is known to
 * substitute for the request's own operation code: it answers the trigger-generator read-back under
 * the acquisition-unit read-back's identifier, and the all-DAC scan under the single-DAC scan's,
 * many times over. Those are the peer's real behavior, so a session that rejected them would fail
 * against the hardware this library exists to talk to.
 *
 * Strict mode drops those allowances and requires the identifier to be the request's operation code
 * and nothing else. It is meant for a caller who has established on hardware that its readout
 * echoes operation codes faithfully, and wants a mis-correlation reported rather than accepted; it
 * is off until then. Nothing else changes: malformed responses and non-correlating ones are handled
 * the same way in both modes.
 *
 * \param u UDP session
 * \param strict True to require the request's own operation code, false (the default) to accept the
 *   firmware's documented substitutions as well
 */
void
katherine_udp_set_strict_ack(katherine_udp_t *u, bool strict)
{
    u->strict_ack = strict;
}

/**
 * Lock mutual exclusion synchronization primitive.
 * \param u UDP session
 *
 * \retval KATHERINE_E_OK on success, with the session's mutex held by the
 *   calling thread. A mutex abandoned by a thread that exited while holding
 *   it counts as ownership too (WAIT_ABANDONED), and is reported as taken
 *   rather than as a failure.
 * \retval KATHERINE_E_SYSTEM if the wait itself failed, which for a mutex
 *   waited on without a timeout means an unusable handle -- a session
 *   katherine_udp_init() never initialized, or one already finalized by
 *   katherine_udp_fini(); see WaitForSingleObject() and
 *   katherine_udp_last_os_error(), which reports the GetLastError() code.
 */
katherine_error_t
katherine_udp_mutex_lock(katherine_udp_t *u)
{
    // Waited on with no timeout, so the only outcomes are ownership
    // (WAIT_OBJECT_0, or WAIT_ABANDONED -- still ownership, just left
    // behind by a thread that exited while holding it) and outright
    // failure, which alone carries a GetLastError() reason.
    DWORD res = WaitForSingleObject(u->mutex, INFINITE);
    if (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED) {
        return KATHERINE_E_OK;
    }

    // Win32 system-error space, not Winsock's; see the CreateMutex() call in
    // katherine_udp_init_bound() for why that means no mapping.
    u->last_os_error = (int) GetLastError();
    return KATHERINE_E_SYSTEM;
}

/**
 * Unlock mutual exclusion synchronization primitive.
 * \param u UDP session
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_SYSTEM if the mutex could not be released -- the
 *   calling thread is not the one holding it (ERROR_NOT_OWNER), or the
 *   handle is unusable, as on a session katherine_udp_init() never
 *   initialized or katherine_udp_fini() already finalized; see
 *   ReleaseMutex() and katherine_udp_last_os_error(), which reports the
 *   GetLastError() code.
 */
katherine_error_t
katherine_udp_mutex_unlock(katherine_udp_t *u)
{
    // ReleaseMutex() returns nonzero on success, the reverse of this
    // library's own 0-on-success convention.
    if (ReleaseMutex(u->mutex)) {
        return KATHERINE_E_OK;
    }

    // Win32 system-error space; see katherine_udp_mutex_lock() above.
    u->last_os_error = (int) GetLastError();
    return KATHERINE_E_SYSTEM;
}

/**
 * Read the OS-level detail of a UDP session's most recent transport failure.
 * \param u UDP session
 * \return The raw OS error code behind the session's last failure -- a
 *   WSAGetLastError() code from the socket calls, a GetLastError() one from
 *   the mutex calls, always in the platform's own numbering and never
 *   translated -- or 0 if the session succeeded, or failed without an OS
 *   error (e.g. a malformed address argument, or a receive that simply found
 *   nothing). A caller reading this must therefore branch per platform to
 *   interpret it; katherine_error_t is the portable layer.
 */
int
katherine_udp_last_os_error(const katherine_udp_t *u)
{
    return u->last_os_error;
}

#endif /* KATHERINE_WIN */
