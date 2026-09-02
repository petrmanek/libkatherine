/**
 * \file
 * \brief Win32 definitions for the UDP communication layer.
 * \author Felix Lehner
 * \date 31.8.18
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

#ifdef KATHERINE_WIN

#include <winsock2.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_udp {
    SOCKET sock;
    SOCKADDR_IN addr_local;
    SOCKADDR_IN addr_remote;

    HANDLE mutex;
    WSADATA wsa_data;

    bool remote_pinned;

    bool strict_ack;                  ///< True to require a command response to repeat the operation code of its request exactly; set through katherine_udp_set_strict_ack().
    uint64_t stray_command_responses; ///< Command response datagrams discarded because they belonged to no request in flight; see katherine_udp_set_strict_ack().

    int last_os_error; ///< 0 unless the last transport operation failed with an OS-level error code; read through katherine_udp_last_os_error().
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_WIN */

/** \} */
