/**
 * @file
 * @brief Functions related to the UDP communication layer.
 * @author Petr Mánek
 * @date 29.5.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>
#include <katherine/global.h>
#include <katherine/udp_nix.h>
#include <katherine/udp_win.h>

/**
 * @addtogroup c_api
 * @{
 */

// Uncomment the following line to enable network trace:
// #define KATHERINE_DEBUG_UDP 2

#ifdef __cplusplus
extern "C" {
#endif

KATHERINE_EXPORTED int
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_EXPORTED int
katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_EXPORTED void
katherine_udp_fini(katherine_udp_t *u);

KATHERINE_EXPORTED int
katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count);

KATHERINE_EXPORTED int
katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count);

KATHERINE_EXPORTED int
katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count);

KATHERINE_EXPORTED int
katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port);

KATHERINE_EXPORTED int
katherine_udp_mutex_lock(katherine_udp_t *u);

KATHERINE_EXPORTED int
katherine_udp_mutex_unlock(katherine_udp_t *u);

#ifdef __cplusplus
}
#endif

/** @} */
