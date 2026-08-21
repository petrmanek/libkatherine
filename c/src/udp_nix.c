/**
 * @file
 * @brief POSIX implementation of the UDP communication layer.
 * @author Petr Mánek
 * @date 29.5.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/global.h>

#ifdef KATHERINE_NIX

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <string.h>
#include <katherine/udp.h>

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

/* True if a datagram received from addr counts as coming from the pinned
   remote of session u.

   Only the host is compared, never the port: a readout answers commands from
   its command port but streams measurement data from another one (1556 vs
   1555 for the emulated readout), and no source port of the firmware is
   specified anywhere, whereas the hazard a pin guards against -- another
   peer's stray datagram becoming the session's remote -- is a property of
   the host. */
static bool
from_pinned_remote(const katherine_udp_t *u, const struct sockaddr_in *addr)
{
    return addr->sin_addr.s_addr == u->addr_remote.sin_addr.s_addr;
}

/* Receives one datagram from the pinned remote of session u, discarding up to
   KATHERINE_UDP_PIN_MAX_DISCARDS datagrams from other hosts on the way there.
   Spending that budget is reported as EAGAIN, the very code the expired
   receive timeout of an idle socket yields, so that no caller needs a
   separate path for it.

   The pinned address is read from addr_remote itself rather than from a copy
   taken when the pin was placed, which is what makes the pin follow
   katherine_udp_set_remote(). */
static int
recv_pinned(katherine_udp_t *u, void *data, size_t count, size_t *received)
{
    for (uint32_t discarded = 0; discarded < KATHERINE_UDP_PIN_MAX_DISCARDS; ++discarded) {
        struct sockaddr_in addr_from;
        socklen_t addr_len = sizeof(addr_from);
        ssize_t res        = recvfrom(u->sock, data, count, 0, (struct sockaddr *) &addr_from, &addr_len);
        if (res == -1) {
            return errno;
        }

        if (from_pinned_remote(u, &addr_from)) {
            *received = (size_t) res;
            return 0;
        }
    }

    return EAGAIN;
}

/* Receives one datagram into data, honoring the pin of session u: a pinned
   session accepts only datagrams from its remote host and leaves addr_remote
   alone, an unpinned one accepts the next datagram from anybody and adopts
   its sender as the remote -- the server behavior of replying to whoever
   asked last. */
static int
recv_datagram(katherine_udp_t *u, void *data, size_t count, size_t *received)
{
    if (u->remote_pinned) {
        return recv_pinned(u, data, count, received);
    }

    socklen_t addr_len = sizeof(u->addr_remote);
    ssize_t res        = recvfrom(u->sock, data, count, 0, (struct sockaddr *) &u->addr_remote, &addr_len);
    if (res == -1) {
        return errno;
    }

    *received = (size_t) res;
    return 0;
}

/**
 * Initialize new UDP session.
 * @param u UDP session to initialize
 * @param local_port Local port number
 * @param remote_addr Remote IP address
 * @param remote_port Remote port number
 * @param timeout_ms Communication timeout in milliseconds (zero if disabled)
 * @return Error code.
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
 * @param u UDP session to initialize
 * @param local_addr Local IP address to bind to, or NULL for the wildcard address (INADDR_ANY)
 * @param local_port Local port number
 * @param remote_addr Remote IP address
 * @param remote_port Remote port number
 * @param timeout_ms Communication timeout in milliseconds (zero if disabled)
 * @return Error code.
 */
int
katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    int res = 0;

    // A session starts out tracking the sender of the datagram it last
    // received (see katherine_udp_pin_remote()). Callers hand this function
    // uninitialized storage, so the default cannot be left to the
    // allocation.
    u->remote_pinned = false;

    // Create socket.
    if ((u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        res = errno;
        goto err_socket;
    }

    // Allow another local process bound to a different local address (e.g.
    // the ksim daemon) to reuse the same port number; without this,
    // a second bind() to 1555 or 1556 on this host fails outright even
    // though the addresses differ.
    int reuseaddr = 1;
    if (setsockopt(u->sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        res = errno;
        goto err_reuseaddr;
    }

    // Setup and bind the socket address.
    u->addr_local.sin_family = AF_INET;
    u->addr_local.sin_port   = htons(local_port);
    if (local_addr == NULL) {
        u->addr_local.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, local_addr, &u->addr_local.sin_addr) <= 0) {
        res = EINVAL;
        goto err_local_addr;
    }

    if (bind(u->sock, (struct sockaddr *) &u->addr_local, sizeof(u->addr_local)) == -1) {
        res = errno;
        goto err_bind;
    }

    if (timeout_ms > 0) {
        // Set socket timeout.
        struct timeval timeout;
        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_usec = 1000 * (timeout_ms % 1000);
        if (setsockopt(u->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
            res = errno;
            goto err_timeout;
        }
    }

    // Set remote socket address.
    u->addr_remote.sin_family = AF_INET;
    u->addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &u->addr_remote.sin_addr) <= 0) {
        res = EINVAL;
        goto err_remote;
    }

    if ((res = pthread_mutex_init(&u->mutex, NULL)) != 0) {
        goto err_mutex;
    }

    return res;

err_mutex:
err_remote:
err_timeout:
err_bind:
err_local_addr:
err_reuseaddr:
    close(u->sock);
err_socket:
    return res;
}

/**
 * Finalize UDP session.
 * @param u UDP session to finalize
 */
void
katherine_udp_fini(katherine_udp_t *u)
{
    close(u->sock);

    // Ignoring return code below.
    (void) pthread_mutex_destroy(&u->mutex);
}

/**
 * Send a message (unreliable).
 * @param u UDP session
 * @param data Message start
 * @param count Message length in bytes
 * @return Error code.
 */
int
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    ssize_t sent;
    size_t total = 0;
    do {
        sent = sendto(u->sock, data + total, count - total, 0, (struct sockaddr *) &u->addr_remote, sizeof(u->addr_remote));
        if (sent == -1) {
            return errno;
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
 * @param u UDP session
 * @param data Inbound buffer start
 * @param count Inbound buffer size in bytes
 * @return Error code.
 */
int
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
{
    size_t received = 0;
    size_t total    = 0;

    // A pinned session verifies every datagram of the message separately, and
    // the discard budget is spent per datagram rather than per message.
    while (total < count) {
        int res = recv_datagram(u, data + total, count - total, &received);
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
 * @param u UDP session
 * @param data Inbound buffer start
 * @param count Inbound buffer size in bytes
 * @return Error code.
 */
int
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
{
    size_t received;
    int res = recv_datagram(u, data, *count, &received);

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
 * @param u UDP session
 * @param remote_addr Remote IP address
 * @param remote_port Remote port number
 * @return Error code.
 */
int
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
{
    struct sockaddr_in addr_remote;
    addr_remote.sin_family = AF_INET;
    addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &addr_remote.sin_addr) <= 0) {
        return EINVAL;
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
 * KATHERINE_UDP_PIN_MAX_DISCARDS of them per receive call before reporting EAGAIN.
 *
 * Only the remote host is pinned, not its port, because a readout answers commands from its
 * command port but streams measurement data from another. Pinning cannot be undone.
 *
 * @param u UDP session
 */
void
katherine_udp_pin_remote(katherine_udp_t *u)
{
    u->remote_pinned = true;
}

/**
 * Lock mutual exclusion synchronization primitive.
 * @param u UDP session
 * @return Error code.
 */
int
katherine_udp_mutex_lock(katherine_udp_t *u)
{
    return pthread_mutex_lock(&u->mutex);
}

/**
 * Unlock mutual exclusion synchronization primitive.
 * @param u UDP session
 * @return Error code.
 */
int
katherine_udp_mutex_unlock(katherine_udp_t *u)
{
    return pthread_mutex_unlock(&u->mutex);
}

#endif /* KATHERINE_NIX */
