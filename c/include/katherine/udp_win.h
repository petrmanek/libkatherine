/**
 * @file
 * @brief Win32 definitions for the UDP communication layer.
 * @author Felix Lehner
 * @date 31.8.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef KATHERINE_WIN

#include <winsock2.h>
#include <windows.h>
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
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_WIN */

/** @} */
