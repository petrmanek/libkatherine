//
// Created by petr on 29.5.18.
//

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

int
katherine_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms);

void
katherine_udp_fini(katherine_udp_t *u);

int
katherine_udp_send_exact(katherine_udp_t* u, const void* data, size_t count);

int
katherine_udp_recv_exact(katherine_udp_t* u, void* data, size_t count);

int
katherine_udp_recv(katherine_udp_t* u, void* data, size_t* count);

int
katherine_udp_mutex_lock(katherine_udp_t *u);

int
katherine_udp_mutex_unlock(katherine_udp_t *u);

#ifdef __cplusplus
}
#endif
