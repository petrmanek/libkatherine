/**
 * @file
 * @brief POSIX definitions for the UDP communication layer.
 * @author Petr Mánek
 * @date 13.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>

#ifdef KATHERINE_NIX

#include <arpa/inet.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_udp {
    int sock;
    struct sockaddr_in addr_local;
    struct sockaddr_in addr_remote;

    pthread_mutex_t mutex;
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_NIX */
