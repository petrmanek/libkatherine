/**
 * @file
 * @brief Win32 implementation of the UDP communication layer.
 * @author Felix Lehner
 * @date 31.8.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/global.h>

void
empty_method()
{ }

#ifdef KATHERINE_WIN

#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <katherine/udp.h>

// Translates receive-path Winsock errors to the portable <errno.h> values
// callers test against (a timed-out receive is EAGAIN on POSIX); everything
// else keeps the raw WSA code, as the other functions in this file do.
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

    // Create communication buffer.
    if ((res = WSAStartup(MAKEWORD(2, 2), &u->wsa_data)) != 0) {
        goto err_wsa_data;
    }

    // Create socket.
    if ((u->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        res = WSAGetLastError();
        goto err_socket;
    }

    // Setup and bind the socket address.
    u->addr_local.sin_family = AF_INET;
    u->addr_local.sin_port   = htons(local_port);
    if (local_addr == NULL) {
        u->addr_local.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, local_addr, &u->addr_local.sin_addr) <= 0) {
        res = WSAGetLastError();
        goto err_local_addr;
    }

    if (bind(u->sock, (const struct sockaddr *) &u->addr_local, sizeof(u->addr_local)) == SOCKET_ERROR) {
        res = WSAGetLastError();
        goto err_bind;
    }

    if (timeout_ms > 0) {
        // Set socket timeout.
        DWORD timeout = timeout_ms;
        if (setsockopt(u->sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) == SOCKET_ERROR) {
            res = WSAGetLastError();
            goto err_timeout;
        }
    }

    // Set remote socket address.
    u->addr_remote.sin_family = AF_INET;
    u->addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &u->addr_remote.sin_addr) <= 0) {
        res = WSAGetLastError();
        goto err_remote;
    }

    if ((u->mutex = CreateMutex(NULL, FALSE, NULL)) == NULL) {
        res = GetLastError();
        goto err_mutex;
    }

    return res;

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
 * @param u UDP session to finalize
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
 * @param u UDP session
 * @param data Message start
 * @param count Message length in bytes
 * @return Error code.
 */
int
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    size_t sent;
    size_t total      = 0;
    const char *cdata = (const char *) data;

    do {
        sent = sendto(u->sock, cdata + total, count - total, 0, (struct sockaddr *) &u->addr_remote, sizeof(u->addr_remote));
        if (sent == SOCKET_ERROR) {
            return WSAGetLastError();
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
    size_t received;
    size_t total       = 0;
    socklen_t addr_len = sizeof(u->addr_remote);
    char *cdata        = (char *) data;

    while (total < count) {
        received = recvfrom(u->sock, cdata + total, count - total, 0, (struct sockaddr *) &u->addr_remote, &addr_len);
        if (received == SOCKET_ERROR) {
            return recv_error_code();
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
    socklen_t addr_len = sizeof(u->addr_remote);
    char *cdata        = (char *) data;
    size_t received    = recvfrom(u->sock, cdata, *count, 0, (struct sockaddr *) &u->addr_remote, &addr_len);

    if (received == SOCKET_ERROR) {
        return recv_error_code();
    }

#ifdef KATHERINE_DEBUG_UDP
    dump_buffer("Received:", data, received);
#endif /* KATHERINE_DEBUG_UDP */

    *count = (size_t) received;
    return 0;
}

/**
 * Repoint the remote address of a UDP session.
 *
 * Note that katherine_udp_recv() and katherine_udp_recv_exact() already update the session's
 * remote address to whoever last sent to it, which is how a server naturally replies to its last
 * peer. This function instead sets the *initial or overriding* destination used for outgoing
 * messages until the next inbound datagram arrives (or until this function is called again) --
 * useful for a session that only ever sends, such as a data-only socket that must be redirected to
 * a peer learned over a different session.
 *
 * @param u UDP session
 * @param remote_addr Remote IP address
 * @param remote_port Remote port number
 * @return Error code.
 */
int
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
{
    SOCKADDR_IN addr_remote;
    addr_remote.sin_family = AF_INET;
    addr_remote.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_addr, &addr_remote.sin_addr) <= 0) {
        return WSAGetLastError();
    }

    u->addr_remote = addr_remote;
    return 0;
}

/**
 * Lock mutual exclusion synchronization primitive.
 * @param u UDP session
 * @return Error code.
 */
int
katherine_udp_mutex_lock(katherine_udp_t *u)
{
    return WaitForSingleObject(u->mutex, INFINITE);
}

/**
 * Unlock mutual exclusion synchronization primitive.
 * @param u UDP session
 * @return Error code.
 */
int
katherine_udp_mutex_unlock(katherine_udp_t *u)
{
    return ReleaseMutex(u->mutex);
}

#endif /* KATHERINE_WIN */
