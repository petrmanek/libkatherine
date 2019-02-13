#pragma once

#include <katherine/global.h>

#ifdef KATHERINE_WIN

#include <Windows.h>
#include <Winsock2.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_udp {
    SOCKET sock;
    SOCKADDR_IN addr_local;
    SOCKADDR_IN addr_remote;

    HANDLE mutex;
} katherine_udp_t;

#ifdef __cplusplus
}
#endif

#endif /* KATHERINE_WIN */
