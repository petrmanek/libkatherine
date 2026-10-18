/**
 * \file
 * \brief Finding readouts on a network.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/udp.h>

/**
 * \defgroup katherine_discover Discovery
 * \ingroup katherine_c_api
 * \brief Asking every readout on a network to identify itself.
 */

/**
 * \addtogroup katherine_discover
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lowest port a readout will answer a discovery on. The firmware reads the
 * reply port out of the request's first two bytes and falls back to its own
 * default below this, which would send every answer to the port a running
 * session is already using.
 */
#define KATHERINE_DISCOVER_MIN_PORT 32

/** One readout that answered. */
typedef struct katherine_discovered {
    char address[KATHERINE_DEVICE_ADDRESS_STR_SIZE]; ///< Address it answered from, naming the link too: "udp://192.168.1.42". Ready to hand to katherine_device_init().
    bool legacy;                                     ///< True if the firmware ignored the reply port and answered on the standard one.
} katherine_discovered_t;

KATHERINE_EXPORTED katherine_error_t
katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count);

#ifdef __cplusplus
}
#endif

/** \} */
