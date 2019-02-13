#pragma once

#include <katherine/global.h>

#ifdef KATHERINE_WIN

#include <Windows.h>
#include <Winsock2.h>
#include <stdint.h>
#pragma comment(lib, "Ws2_32.lib")

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
