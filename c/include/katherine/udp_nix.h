/* Katherine Control Library
 *
 * This file was created on 13.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief POSIX definitions for the UDP communication layer.
 */

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
