/**
 * \file
 * \brief POSIX definitions for the UDP communication layer.
 * \author Petr Mánek
 * \date 13.2.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>

/**
 * \addtogroup c_api
 * \{
 */

#ifdef KATHERINE_NIX

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_udp {
    int sock;
    struct sockaddr_in addr_local;
    struct sockaddr_in addr_remote;

    pthread_mutex_t mutex;

    bool remote_pinned;

    bool strict_ack;                  ///< True to require a command response to repeat the operation code of its request exactly; set through katherine_udp_set_strict_ack().
    uint64_t stray_command_responses; ///< Command response datagrams discarded because they belonged to no request in flight; see katherine_udp_set_strict_ack().

    int last_os_error; ///< 0 unless the last transport operation failed with an OS-level error code; read through katherine_udp_last_os_error().
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_NIX */

/** \} */
