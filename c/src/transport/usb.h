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

#include <stddef.h>
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

KATHERINE_NOT_EXPORTED const char *
katherine_str_usb_stream(katherine_usb_stream_t stream);

// The readout's firmware fixed these, and nothing here may choose them. The
// response and data endpoints are the reverse of the obvious order.

/// Endpoint a readout reads commands from.
#define KATHERINE_USB_EP_CMD_OUT      ((uint8_t) 0x02)

/// Endpoint a readout writes command responses to.
#define KATHERINE_USB_EP_CMD_IN       ((uint8_t) 0x82)

/// Endpoint a readout writes measurement data to.
#define KATHERINE_USB_EP_DATA_IN      ((uint8_t) 0x81)

/**
 * Tiers a USB tree can be deep, which is what the specification allows and so
 * the longest chain of port numbers a readout can sit at the end of.
 */
#define KATHERINE_USB_PORT_DEPTH      7

/**
 * Bytes the readout's measurement endpoint sends per data frame.
 *
 * Fixed in the FPGA rather than negotiated on the bus: the streamer emits
 * 170 six-byte words and then four zero bytes, which is where the figure
 * comes from. Deriving it from the endpoint's maximum packet size happens
 * to agree at SuperSpeed and disagrees at every other speed.
 */
#define KATHERINE_USB_DATA_FRAME_SIZE 1024

/**
 * Readout to open, and the strategy for reading it.
 */
typedef struct katherine_usb_config {
    /**
     * Bus the readout is on, as the host numbers them. Meaningful only
     * together with a nonzero port_count.
     */
    uint8_t bus;

    /**
     * Chain of port numbers from the root hub down to the readout, which is
     * what tells two identical readouts apart. Stable across a replug into
     * the same socket and across a device reset, unlike the address the host
     * assigns at enumeration; it changes when the cable moves or a hub is
     * added above it.
     */
    uint8_t ports[KATHERINE_USB_PORT_DEPTH];

    uint8_t port_count; ///< The number of items in `ports` that are valid, or zero to select the first compatible device. See: "usb://auto"

    /**
     * Readout already opened and claimed, which this transport drives without
     * closing, or NULL to open and claim one of its own. Given together with
     * \p handle or not at all.
     */
    struct libusb_context *ctx;

    struct libusb_device_handle *handle; ///< Handle belonging to \p ctx; see it.

    uint8_t ep_out;                ///< Endpoint written to, or zero for a transport that sends nothing.
    uint8_t ep_in;                 ///< Endpoint read from, or zero for a transport that receives nothing.
    katherine_usb_stream_t stream; ///< Read strategy, where a ring was asked for.
    uint32_t queue_len;            ///< Reads kept outstanding by KATHERINE_USB_STREAM_ASYNC.
    uint32_t buffer_size;          ///< Bytes one read can carry.
    uint32_t timeout_ms;           ///< Time a read waits before reporting KATHERINE_E_TIMEOUT.
} katherine_usb_config_t;

KATHERINE_NOT_EXPORTED katherine_usb_config_t
katherine_usb_config_default(void);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_usb_config_set_address(katherine_usb_config_t *config, const char *address);

KATHERINE_NOT_EXPORTED int
katherine_usb_config_snprint_address(char *buf, size_t cap, const katherine_usb_config_t *config);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_transport_init_usb(katherine_transport_t *transport, const katherine_usb_config_t *config);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_usb_transport_framing_errors(const katherine_transport_t *transport, uint64_t *count);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_usb_borrow_device(katherine_usb_config_t *config, const katherine_transport_t *from);

KATHERINE_NOT_EXPORTED katherine_error_t
katherine_usb_inflight(const katherine_transport_t *transport, uint32_t *count);


#ifdef __cplusplus
}
#endif

/** \} */
