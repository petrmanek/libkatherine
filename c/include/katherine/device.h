//
// Created by petr on 14.6.18.
//

#pragma once

/**
 * @file
 * @brief Functions related to Katherine.
 */

#include <katherine/global.h>
#include <katherine/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_device {
    katherine_udp_t control_socket;
    katherine_udp_t data_socket;
} katherine_device_t;

KATHERINE_EXPORTED int
katherine_device_init(katherine_device_t *, const char *);

KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *);

#ifdef __cplusplus
}
#endif
