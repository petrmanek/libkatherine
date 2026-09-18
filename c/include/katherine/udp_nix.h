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
 * \addtogroup katherine_udp
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

/** One UDP session: a bound socket, the endpoint it talks to, and the lock serializing it. */
typedef struct katherine_udp {
    int sock;                       ///< The OS socket handle.
    struct sockaddr_in addr_local;  ///< Local endpoint this session is bound to.
    struct sockaddr_in addr_remote; ///< Remote endpoint it sends to; see katherine_udp_set_remote().

    pthread_mutex_t mutex; ///< Serializes use of the session between threads; taken through katherine_udp_mutex_lock().

    bool remote_pinned; ///< True to keep addr_remote fixed and discard datagrams from any other host; set through katherine_udp_pin_remote().

    bool strict_ack;                  ///< True to require a command response to repeat the operation code of its request exactly; set through katherine_udp_set_strict_ack().
    uint64_t stray_command_responses; ///< Command response datagrams discarded because they belonged to no request in flight; see katherine_udp_set_strict_ack().

    int last_os_error; ///< 0 unless the last transport operation failed with an OS-level error code; read through katherine_udp_last_os_error().
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_NIX */

/** \} */
