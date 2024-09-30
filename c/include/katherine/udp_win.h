/* Katherine Control Library
 *
 * This file was created on 31.8.18 by Felix Lehner.
 * This file was modified on 13.2.19 by Petr Manek.
 *
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief Win32 definitions for the UDP communication layer.
 */

#include <katherine/global.h>

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
