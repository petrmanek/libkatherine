//
// Created by petr on 29.5.18.
//

#ifndef THESIS_UDP_H
#define THESIS_UDP_H

/**
 * @file
 * @brief Functions related to the UDP communication layer.
 */

#include <stdio.h>
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

#endif //THESIS_UDP_H
