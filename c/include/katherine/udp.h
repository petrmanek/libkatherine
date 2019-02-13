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
katherine_udp_init(katherine_udp_t *, uint16_t, const char *, uint16_t, uint32_t);

void
katherine_udp_fini(katherine_udp_t *);

int
katherine_udp_send_exact(katherine_udp_t*, const void*, size_t);

int
katherine_udp_recv_exact(katherine_udp_t*, void*, size_t);

int
katherine_udp_recv(katherine_udp_t*, void*, size_t*);

int
katherine_udp_mutex_lock(katherine_udp_t*);

int
katherine_udp_mutex_unlock(katherine_udp_t*);

#ifdef __cplusplus
}
#endif
