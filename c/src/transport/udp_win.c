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

/**
 * Translates receive-path Winsock errors to the portable `<errno.h>` values
 * callers test against (a timed-out receive is EAGAIN on POSIX); everything
 * else keeps the raw WSA code, as the other functions in this file do.
 * \return A portable code, or the raw WSA code if none applies
 */
static int
recv_error_code(void)
{
    int err = WSAGetLastError();
    switch (err) {
    case WSAETIMEDOUT:
    case WSAEWOULDBLOCK: return EAGAIN;
    case WSAEINTR:       return EINTR;
    default:             return err;
    }
}

// Maps the portable-ish value recv_error_code() above produces (EAGAIN,
// EINTR, or a passed-through raw WSA code) -- or, at every other syscall
// site in this file, a raw WSA/GetLastError() code directly -- to the
// library's own error domain. The three cases every public function agrees
// on (EAGAIN/EWOULDBLOCK/ETIMEDOUT as a timeout, EINVAL, ENOMEM) apply
// wherever they turn up; anything else, including a WSA-space code that
// numerically matches none of them, falls back to the group the caller
// names. The OS-level detail is preserved separately, in
// katherine_udp_t::last_os_error.
static katherine_error_t
map_syscall_error(int err, katherine_error_t fallback)
{
    switch (err) {
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
        return KATHERINE_E_TIMEOUT;
    case EINVAL:
        return KATHERINE_E_INVAL;
    case ENOMEM:
        return KATHERINE_E_NOMEM;
    default:
        return fallback;
    }
}

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

// Reports whether a datagram is already queued on the socket of session u,
// without waiting for one to arrive: 0 if the recvfrom() that follows will
// not block, -KATHERINE_E_TIMEOUT if the socket is empty -- the same code the
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
static int
recv_ready(katherine_udp_t *u)
{
    u_long available = 0;

    if (ioctlsocket(u->sock, FIONREAD, &available) == SOCKET_ERROR) {
        u->last_os_error = WSAGetLastError();
        return -(int) map_syscall_error(u->last_os_error, KATHERINE_E_IO);
    }

    if (available == 0) {
        u->last_os_error = 0;
        return -KATHERINE_E_TIMEOUT;
    }

    return 0;
}

// Receives one datagram from the pinned remote of session u, discarding up to
// KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts on the way there.
// Spending that budget is reported as -KATHERINE_E_TIMEOUT, the very code
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
static int
recv_pinned(katherine_udp_t *u, void *data, size_t count, size_t *received, bool nowait)
{
    char *cdata = (char *) data;

    for (uint32_t discarded = 0; discarded < KATHERINE_UDP_PIN_MAX_DISCARDS; ++discarded) {
        SOCKADDR_IN addr_from;
        socklen_t addr_len = sizeof(addr_from);

        if (nowait) {
            int ready = recv_ready(u);
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
                return 0;
            }

            int raw          = recv_error_code();
            u->last_os_error = raw;
            return -(int) map_syscall_error(raw, KATHERINE_E_IO);
        }

        if (from_pinned_remote(u, &addr_from)) {
            *received = (size_t) res;
            return 0;
        }
    }

    // The discard budget is spent, not an OS-level failure -- reported
    // exactly like an expired receive timeout (see KATHERINE_UDP_PIN_MAX_DISCARDS,
    // katherine/udp.h), so no caller needs a separate path for it.
    u->last_os_error = 0;
    return -KATHERINE_E_TIMEOUT;
}

// Receives one datagram into data, honoring the pin of session u: a pinned
// session accepts only datagrams from its remote host and leaves addr_remote
// alone, an unpinned one accepts the next datagram from anybody and adopts
// its sender as the remote -- the server behavior of replying to whoever
// asked last.
static int
recv_datagram(katherine_udp_t *u, void *data, size_t count, size_t *received, bool nowait)
{
    char *cdata = (char *) data;

    if (u->remote_pinned) {
        return recv_pinned(u, cdata, count, received, nowait);
    }

    if (nowait) {
        int ready = recv_ready(u);
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
            return 0;
        }

        int raw          = recv_error_code();
        u->last_os_error = raw;
        return -(int) map_syscall_error(raw, KATHERINE_E_IO);
    }

    *received = (size_t) res;
    return 0;
}

/**
 * Initialize new UDP session.
 * \param u UDP session to initialize
 * \param local_port Local port number
 * \param remote_addr Remote IP address
 * \param remote_port Remote port number
 * \param timeout_ms Communication timeout in milliseconds (zero if disabled)
 * \return Error code.
 */
int
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
 * \return Error code.
 */
int
katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    int res = 0;

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
        res              = -(int) map_syscall_error(wres, KATHERINE_E_SYSTEM);
        goto err_wsa_data;
    }

    // Create socket.
    if ((u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        u->last_os_error = WSAGetLastError();
        res              = -(int) map_syscall_error(u->last_os_error, KATHERINE_E_IO);
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
        res = -KATHERINE_E_ADDR;
        goto err_local_addr;
    }

    if (bind(u->sock, (const struct sockaddr *) &u->addr_local, sizeof(u->addr_local)) == SOCKET_ERROR) {
        u->last_os_error = WSAGetLastError();
        res              = -KATHERINE_E_ADDR;
        goto err_bind;
    }

    if (timeout_ms > 0) {
        // Set socket timeout.
        DWORD timeout = timeout_ms;
        if (setsockopt(u->sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) == SOCKET_ERROR) {
            u->last_os_error = WSAGetLastError();
            res              = -(int) map_syscall_error(u->last_os_error, KATHERINE_E_IO);
            goto err_timeout;
        }
    }

    // Set remote socket address.
    u->addr_remote.sin_family = AF_INET;
    u->addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &u->addr_remote.sin_addr) <= 0) {
        res = -KATHERINE_E_ADDR;
        goto err_remote;
    }

    if ((u->mutex = CreateMutex(NULL, FALSE, NULL)) == NULL) {
        u->last_os_error = (int) GetLastError();
        res              = -(int) map_syscall_error(u->last_os_error, KATHERINE_E_SYSTEM);
        goto err_mutex;
    }

    return 0;

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
 * \return Error code.
 */
int
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    size_t sent;
    size_t total      = 0;
    const char *cdata = (const char *) data;

    do {
        sent = sendto(u->sock, cdata + total, (int) (count - total), 0, (struct sockaddr *) &u->addr_remote, sizeof(u->addr_remote));
        if (sent == SOCKET_ERROR) {
            u->last_os_error = WSAGetLastError();
            return -(int) map_syscall_error(u->last_os_error, KATHERINE_E_IO);
        }

        total += sent;
    } while (total < count);

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Sent:", data, count);
#endif /* KATHERINE_DEBUG_UDP */

    return 0;
}

/**
 * Receive a message (unreliable).
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes
 * \return Error code.
 */
int
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
{
    size_t received = 0;
    size_t total    = 0;
    char *cdata     = (char *) data;

    // A pinned session verifies every datagram of the message separately, and
    // the discard budget is spent per datagram rather than per message.
    while (total < count) {
        int res = recv_datagram(u, cdata + total, count - total, &received, false);
        if (res != 0) {
            return res;
        }

        total += received;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    return 0;
}

/**
 * Receive a portion of a message (unreliable).
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes
 * \return Error code.
 */
int
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received;
    int res = recv_datagram(u, data, *count, &received, false);

    if (res != 0) {
        return res;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    *count = received;
    return 0;
}

/**
 * Receive a portion of a message without waiting for one.
 *
 * Identical to katherine_udp_recv(), except that a session with nothing
 * already queued reports -KATHERINE_E_TIMEOUT at once instead of blocking
 * for its receive timeout. That is what makes it usable to flush a session
 * before a command exchange: the flush must cost nothing on the empty
 * socket it finds in the ordinary case, which every command of the library
 * would otherwise pay for.
 *
 * \param u UDP session
 * \param data Inbound buffer start
 * \param count Inbound buffer size in bytes on entry, bytes received on
 *   success
 * \return Error code. -KATHERINE_E_TIMEOUT if nothing was queued.
 */
int
katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received;
    int res = recv_datagram(u, data, *count, &received, true);

    if (res != 0) {
        return res;
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    *count = received;
    return 0;
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
 * \return Error code.
 */
int
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
{
    SOCKADDR_IN addr_remote;
    addr_remote.sin_family = AF_INET;
    addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &addr_remote.sin_addr) <= 0) {
        // Same address-resolution failure as katherine_udp_init_bound(), and
        // not an OS-level one either.
        u->last_os_error = 0;
        return -KATHERINE_E_ADDR;
    }

    u->addr_remote = addr_remote;
    return 0;
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
 * -KATHERINE_E_TIMEOUT.
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
 * \return Error code.
 */
int
katherine_udp_mutex_lock(katherine_udp_t *u)
{
    // Waited on with no timeout, so the only outcomes are ownership
    // (WAIT_OBJECT_0, or WAIT_ABANDONED -- still ownership, just left
    // behind by a thread that exited while holding it) and outright
    // failure, which alone carries a GetLastError() reason.
    DWORD res = WaitForSingleObject(u->mutex, INFINITE);
    if (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED) {
        return 0;
    }

    u->last_os_error = (int) GetLastError();
    return -(int) map_syscall_error(u->last_os_error, KATHERINE_E_SYSTEM);
}

/**
 * Unlock mutual exclusion synchronization primitive.
 * \param u UDP session
 * \return Error code.
 */
int
katherine_udp_mutex_unlock(katherine_udp_t *u)
{
    // ReleaseMutex() returns nonzero on success, the reverse of this
    // library's own 0-on-success convention.
    if (ReleaseMutex(u->mutex)) {
        return 0;
    }

    u->last_os_error = (int) GetLastError();
    return -(int) map_syscall_error(u->last_os_error, KATHERINE_E_SYSTEM);
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

#endif /* KATHERINE_WIN */
