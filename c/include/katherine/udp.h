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

#include <stdio.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/udp_nix.h>
#include <katherine/udp_win.h>

/**
 * \addtogroup c_api
 * \{
 */

// Uncomment the following line to enable network trace:
// #define KATHERINE_DEBUG_UDP 2

// Datagrams from another host that one receive call of a pinned session
// (see katherine_udp_pin_remote()) discards before it gives up and reports
// KATHERINE_E_TIMEOUT. Each discard rearms the socket's receive timeout, so
// an unbounded loop would let a chatty stray source stretch a call of a
// session with a 100 ms timeout for as long as it kept sending.
#define KATHERINE_UDP_PIN_MAX_DISCARDS 32

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Render a UDP session: its local and remote endpoints (dotted-quad IPv4
 * address and port, hand-formatted -- no inet_ntop(), so this needs no
 * platform-specific include beyond what this header already pulls in),
 * whether its remote address is pinned (see katherine_udp_pin_remote()) and
 * its command-response correlation state (see
 * katherine_udp_set_strict_ack()). The socket handle and the mutex are
 * omitted: neither is meaningful in a log line, and the mutex additionally
 * has no portable readable state.
 */
KATHERINE_EXPORTED int
katherine_udp_snprint(char *buf, size_t cap, const katherine_udp_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_EXPORTED void
katherine_udp_fini(katherine_udp_t *u);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port);

KATHERINE_EXPORTED void
katherine_udp_pin_remote(katherine_udp_t *u);

KATHERINE_EXPORTED void
katherine_udp_set_strict_ack(katherine_udp_t *u, bool strict);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_mutex_lock(katherine_udp_t *u);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_mutex_unlock(katherine_udp_t *u);

// The OS-level detail behind a session's last transport failure (0 if it
// succeeded, or failed without one -- see katherine/error.h).
KATHERINE_EXPORTED int
katherine_udp_last_os_error(const katherine_udp_t *u);

#ifdef __cplusplus
}
#endif

/** \} */
