/**
 * \file
 * \brief Functions related to the UDP communication layer.
 * \author Petr Mánek
 * \date 29.5.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/transport.h>

// A session is a socket and a lock, and both are the platform's own. On
// Windows this must precede any <windows.h>, which internally reaches for the
// legacy Winsock 1.1 header unless Winsock 2 was established first.
#ifdef KATHERINE_WIN
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
#endif

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// Uncomment the following line to enable network trace:
// #define KATHERINE_DEBUG_UDP 2

/// The maximum number of consecutively discarded datagrams before a timeout
/// condition is reported.
#define KATHERINE_UDP_PIN_MAX_DISCARDS 32

/// Largest UDP receive buffer a session asks the OS for when it opens, the OS
/// default being insufficiently small. A host that refuses it is asked for
/// successively smaller buffers, so a session may hold less than this; what it
/// actually holds is read through katherine_udp_rcvbuf().
#define KATHERINE_UDP_RCVBUF_DEFAULT   4194304

#ifdef __cplusplus
extern "C" {
#endif

/** One UDP session: a bound socket, the endpoint it talks to, and the lock serializing it. */
typedef struct katherine_udp {
#ifdef KATHERINE_WIN
    SOCKET sock;                ///< The OS socket handle.
    SOCKADDR_IN address_local;  ///< Local endpoint this session is bound to.
    SOCKADDR_IN address_remote; ///< Remote endpoint it sends to; see katherine_udp_set_remote().
    HANDLE mutex;               ///< Serializes use of the session between threads; taken through katherine_udp_mutex_lock().
    WSADATA wsa_data;           ///< Winsock state this session initialized, released by katherine_udp_fini().
#else
    int sock;                          ///< The OS socket handle.
    struct sockaddr_in address_local;  ///< Local endpoint this session is bound to.
    struct sockaddr_in address_remote; ///< Remote endpoint it sends to; see katherine_udp_set_remote().
    pthread_mutex_t mutex;             ///< Serializes use of the session between threads; taken through katherine_udp_mutex_lock().

    // Winsock keeps no per-socket count of what it dropped for want of queue
    // space, and attributing its per-interface statistics to one session
    // would be a guess, so these are the POSIX side's alone.
    bool count_drops;           ///< True to ask the kernel how many datagrams it dropped for want of queue space; set through katherine_udp_set_drop_counting().
    uint32_t last_drop_count;   ///< Internal: the kernel's own cumulative counter as of the last datagram, which wraps.
    uint64_t dropped_datagrams; ///< Datagrams the kernel dropped for want of queue space; read through katherine_udp_get_dropped_datagrams().
#endif

    bool remote_pinned; ///< True to keep address_remote fixed and discard datagrams from any other host; set through katherine_udp_pin_remote().
    int last_os_error;  ///< 0 unless the last transport operation failed with an OS-level error code; read through katherine_udp_last_os_error().
} katherine_udp_t;

KATHERINE_NOT_EXPORTED int
katherine_udp_snprint(char *buf, size_t cap, const katherine_udp_t *v);

/** A UDP session to open: which ports it binds and talks to, and how it behaves once open. */
typedef struct katherine_udp_config {
    const char *local_address;  ///< Address to bind to, or NULL for every address the host has.
    uint16_t local_port;        ///< Port to bind to; zero asks the OS for a free one.
    const char *remote_address; ///< Readout this session addresses.
    uint16_t remote_port;       ///< Port on the readout.
    uint32_t timeout_ms;        ///< Time a receive waits before reporting KATHERINE_E_TIMEOUT.
    uint32_t rcvbuf;            ///< Receive queue to ask the OS for, or zero for the session's own default.
    bool count_drops;           ///< True to ask the kernel how many datagrams it dropped for want of queue space; see katherine_transport_get_dropped_input().
    bool pin_remote;            ///< True to refuse datagrams from anywhere but the remote named here; see katherine_udp_pin_remote().
} katherine_udp_config_t;

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_transport_init_udp(katherine_transport_t *transport, const katherine_udp_config_t *config);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_address, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_init_bound(katherine_udp_t *u, const char *local_address, uint16_t local_port, const char *remote_address, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_NOT_EXPORTED void
katherine_udp_fini(katherine_udp_t *u);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_address, uint16_t remote_port);

KATHERINE_NOT_EXPORTED void
katherine_udp_pin_remote(katherine_udp_t *u);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_mutex_lock(katherine_udp_t *u);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_mutex_unlock(katherine_udp_t *u);

KATHERINE_NOT_EXPORTED int
katherine_udp_last_os_error(const katherine_udp_t *u);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_set_timeout(katherine_udp_t *u, uint32_t timeout_ms);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_set_rcvbuf(katherine_udp_t *u, uint32_t bytes);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_rcvbuf(const katherine_udp_t *u, uint32_t *bytes);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_set_drop_counting(katherine_udp_t *u, bool enabled);

KATHERINE_NOT_EXPORTED uint64_t
katherine_udp_get_dropped_datagrams(const katherine_udp_t *u);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_set_broadcast(katherine_udp_t *u, bool enabled);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_udp_get_remote_address(const katherine_udp_t *u, char *address, size_t address_size);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_transport_udp_bound_port(const katherine_transport_t *transport, uint16_t *port);


#ifdef __cplusplus
}
#endif

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
