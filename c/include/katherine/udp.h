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
 * \defgroup katherine_udp UDP transport
 * \ingroup katherine_c_api
 * \brief The datagram sessions carrying the control and data planes.
 */

/**
 * \addtogroup katherine_udp
 * \{
 */

// Uncomment the following line to enable network trace:
// #define KATHERINE_DEBUG_UDP 2

/// The maximum number of consecutively discarded datagrams before a timeout
/// condition is reported.
#define KATHERINE_UDP_PIN_MAX_DISCARDS 32

/// Initial size of a UDP receive buffer, as requested from the OS when the
/// socket is first instantiated. The OS default is insufficiently small.
#define KATHERINE_UDP_RCVBUF_DEFAULT   4194304

#ifdef __cplusplus
extern "C" {
#endif

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

KATHERINE_EXPORTED int
katherine_udp_last_os_error(const katherine_udp_t *u);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_set_rcvbuf(katherine_udp_t *u, uint32_t bytes);

KATHERINE_EXPORTED katherine_error_t
katherine_udp_rcvbuf(const katherine_udp_t *u, uint32_t *bytes);

#ifdef __cplusplus
}
#endif

/** \} */
