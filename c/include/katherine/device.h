/**
 * @file
 * @brief Functions related to Katherine.
 * @author Petr Mánek
 * @date 14.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <katherine/global.h>
#include <katherine/udp.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_device {
    katherine_udp_t control_socket; ///< Slow control communication channel, which carries commands and acknowledgements (full duplex).
    katherine_udp_t data_socket;    ///< Measurement data (MD) communication channel, only used during acquisition (half duplex towards this system).
    void *acquisition;              ///< The acquisition measuring on this device, or NULL when none is. Opaque by design.
} katherine_device_t;

KATHERINE_EXPORTED int
katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v);

KATHERINE_EXPORTED int
katherine_device_init(katherine_device_t *device, const char *addr);

KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *device);

KATHERINE_EXPORTED bool
katherine_device_can_correct_timestamp_phase(const katherine_device_t *device);

#ifdef __cplusplus
}
#endif

/** @} */
