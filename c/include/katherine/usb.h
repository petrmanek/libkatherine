/**
 * \file
 * \brief Driving a readout over bulk endpoints on a USB bus.
 *
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/transport.h>

/**
 * \addtogroup katherine_transport
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read strategy for the measurement-data endpoint.
 *
 * A bulk IN endpoint only moves data while the host has a read outstanding,
 * so whichever of these is chosen decides whether the bus carries anything
 * while the caller is decoding what it last received.
 */
typedef enum katherine_usb_stream {
    /**
     * Several reads outstanding at once, refilled as the caller gives buffers
     * back. The bus stays busy through the caller's decoding.
     */
    KATHERINE_USB_STREAM_ASYNC = 0,

    /**
     * One read at a time, issued when the caller asks for data. Nothing
     * crosses the bus while the caller is decoding, and the readout's buffer
     * absorbs the gap or overflows.
     */
    KATHERINE_USB_STREAM_SYNC,
} katherine_usb_stream_t;

KATHERINE_EXPORTED const char *
katherine_str_usb_stream(katherine_usb_stream_t stream);

/**
 * Readout to open, and the strategy for reading it.
 *
 * \note The interface field is named `iface` because the Windows COM headers
 *   define `interface` as a macro.
 */
typedef struct katherine_usb_config {
    katherine_usb_stream_t stream; ///< Read strategy for the measurement-data endpoint.
    uint32_t queue_len;            ///< Reads kept outstanding by KATHERINE_USB_STREAM_ASYNC.
    uint32_t buffer_size;          ///< Bytes one read can carry.
    uint32_t timeout_ms;           ///< Time a read waits before reporting KATHERINE_E_TIMEOUT.
} katherine_usb_config_t;

KATHERINE_EXPORTED katherine_usb_config_t
katherine_usb_config_default(void);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_init_usb(katherine_transport_t *transport, const katherine_usb_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_usb_inflight(const katherine_transport_t *transport, uint32_t *count);

#ifdef __cplusplus
}
#endif

/** \} */
