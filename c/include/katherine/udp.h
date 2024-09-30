/* Katherine Control Library
 *
 * This file was created on 29.5.18 by Petr Manek
 *
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief Functions related to the UDP communication layer.
 */

#include <stdio.h>
#include <katherine/global.h>
#include <katherine/udp_nix.h>
#include <katherine/udp_win.h>

// Uncomment the following line to enable network trace:
// #define KATHERINE_DEBUG_UDP 2

#ifdef __cplusplus
extern "C" {
#endif

KATHERINE_EXPORTED int
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

KATHERINE_EXPORTED void
katherine_udp_fini(katherine_udp_t *u);

KATHERINE_EXPORTED int
katherine_udp_send_exact(katherine_udp_t* u, const void* data, size_t count);

KATHERINE_EXPORTED int
katherine_udp_recv_exact(katherine_udp_t* u, void* data, size_t count);

KATHERINE_EXPORTED int
katherine_udp_recv(katherine_udp_t* u, void* data, size_t* count);

KATHERINE_EXPORTED int
katherine_udp_mutex_lock(katherine_udp_t *u);

KATHERINE_EXPORTED int
katherine_udp_mutex_unlock(katherine_udp_t *u);

#ifdef __cplusplus
}
#endif
