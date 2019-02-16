/* Katherine Control Library
 *
 * This file was created on 14.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

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
katherine_device_init(katherine_device_t *device, const char *addr);

KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *device);

#ifdef __cplusplus
}
#endif
