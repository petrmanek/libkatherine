/**
 * \file
 * \brief Internal construction of the transports a device drives.
 * \author Petr Mánek
 * \date 20.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/global.h>
#include <katherine/transport.h>

#include "../address.h"
#ifdef KATHERINE_FEATURE_UDP
#include "udp.h"
#endif /* KATHERINE_FEATURE_UDP */
#ifdef KATHERINE_FEATURE_USB
#include "usb.h"
#endif

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef __cplusplus
extern "C" {
#endif

// The standard pair a readout serves, and the port it listens on.
#define KATHERINE_UDP_CONTROL_PORT    ((uint16_t) 1555)
#define KATHERINE_UDP_DATA_PORT       ((uint16_t) 1556)
#define KATHERINE_UDP_REMOTE_PORT     ((uint16_t) 1555)

// What a session waits for an answer that may never come.
#define KATHERINE_UDP_CONTROL_TIMEOUT ((uint32_t) 100)
#define KATHERINE_UDP_DATA_TIMEOUT    ((uint32_t) 100)

/**
 * Open the transport a readout's commands travel on.
 * \param address Readout to open, naming its link.
 * \param private_udp_ports True to bind a port of the operating system's
 *   choosing rather than KATHERINE_UDP_CONTROL_PORT. Meaningless to a link
 *   with no port space.
 * \param control Transport to fill in.
 *
 * A UDP session is pinned to the address given, so a datagram from any other
 * sender is discarded rather than taken for an answer. A USB transport opens
 * and claims the readout's interface, and so is the one to finalise last.
 *
 * Nothing is left open on failure, and the transport is zeroed first, so a
 * caller that finalises after a refusal finalises nothing.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if an argument is missing.
 * \retval KATHERINE_E_ADDRESS if the address names no link this build
 *   carries, or the backend could not read the remainder as one of its own.
 * \retval KATHERINE_E_BUSY if the local port is already held by another
 *   process -- another libkatherine on this host is the usual cause; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_UNSUPPORTED if the address names a link this build
 *   cannot open.
 * \retval KATHERINE_E_NOMEM if the transport or its buffer could not be
 *   allocated.
 * \retval KATHERINE_E_IO if the backend's constructor failed at the OS level
 *   for a reason none of the other codes name.
 * \retval KATHERINE_E_TIMEOUT if one of its calls reported that it would have
 *   blocked.
 * \retval KATHERINE_E_CLOSED if the readout went away while it was opening.
 * \retval KATHERINE_E_SYSTEM if a lock could not be created.
 */
static inline katherine_error_t
katherine_transport_init_for_control(const char *address, bool private_udp_ports, katherine_transport_t *control)
{
    katherine_error_t res;

#ifndef KATHERINE_FEATURE_UDP
    // Only a link with a port space reads this, and none was built in.
    (void) private_udp_ports;
#endif /* KATHERINE_FEATURE_UDP */

    if (address == NULL || control == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    memset(control, 0, sizeof(*control));

    katherine_transport_kind_t kind;
    const char *rest;
    if ((res = katherine_address_split(address, &kind, &rest)) != KATHERINE_E_OK) {
        goto err_address;
    }

    switch (kind) {
#ifdef KATHERINE_FEATURE_UDP
    case KATHERINE_TRANSPORT_UDP: {
        const katherine_udp_config_t session = {
            .local_port     = private_udp_ports ? 0 : KATHERINE_UDP_CONTROL_PORT,
            .remote_address = rest,
            .remote_port    = KATHERINE_UDP_REMOTE_PORT,
            .timeout_ms     = KATHERINE_UDP_CONTROL_TIMEOUT,
            .pin_remote     = true,
        };

        res = katherine_transport_init_udp(control, &session);
        break;
    }
#endif /* KATHERINE_FEATURE_UDP */

#ifdef KATHERINE_FEATURE_USB
    case KATHERINE_TRANSPORT_USB: {
        katherine_usb_config_t config = katherine_usb_config_default();

        config.ep_out    = KATHERINE_USB_EP_CMD_OUT;
        config.ep_in     = KATHERINE_USB_EP_CMD_IN;
        config.queue_len = 0;

        if ((res = katherine_usb_config_set_address(&config, address)) == KATHERINE_E_OK) {
            res = katherine_transport_init_usb(control, &config);
        }
        break;
    }
#endif /* KATHERINE_FEATURE_USB */

    default:
        res = KATHERINE_E_UNSUPPORTED;
        break;
    }

err_address:
err_args:
    return res;
}

/**
 * Open the transport a readout's measurement data travels on.
 * \param address Readout to open, naming its link.
 * \param control The readout's control transport, open.
 * \param local_udp_port Port to bind, which the readout was told to send to.
 *   Meaningless to a link with no port space.
 * \param data Transport to fill in.
 *
 * \p control is what a link that cannot be claimed twice is driven through:
 * a USB data transport takes the readout its control transport opened and
 * closes nothing, so \p control is finalised after it. A UDP session opens
 * one of its own and reads \p control only to refuse a mismatched link.
 *
 * Counting of datagrams the kernel dropped is asked for here and nowhere
 * else, this being the only session a measurement stream arrives on.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if an argument is missing, or \p control is not
 *   open on the link the address names.
 * \retval KATHERINE_E_ADDRESS if the address names no link this build
 *   carries, or the backend could not read the remainder as one of its own.
 * \retval KATHERINE_E_BUSY if the local port is already held by another
 *   process -- another libkatherine on this host is the usual cause; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_UNSUPPORTED if the address names a link this build
 *   cannot open.
 * \retval KATHERINE_E_NOMEM if the transport, its ring or its buffers could
 *   not be allocated.
 * \retval KATHERINE_E_IO if the backend's constructor failed at the OS level
 *   for a reason none of the other codes name.
 * \retval KATHERINE_E_TIMEOUT if one of its calls reported that it would have
 *   blocked.
 * \retval KATHERINE_E_CLOSED if the readout went away while it was opening.
 * \retval KATHERINE_E_SYSTEM if a lock could not be created.
 */
static inline katherine_error_t
katherine_transport_init_for_data(const char *address, const katherine_transport_t *control, uint16_t local_udp_port, size_t md_buffer_size, size_t *actual_buffer_size, katherine_transport_t *data)
{
    katherine_error_t res;

#ifndef KATHERINE_FEATURE_UDP
    // Only a link with a port space reads this, and none was built in.
    (void) local_udp_port;
#endif /* KATHERINE_FEATURE_UDP */

    if (address == NULL || control == NULL || data == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    memset(data, 0, sizeof(*data));

    katherine_transport_kind_t kind;
    const char *rest;
    if ((res = katherine_address_split(address, &kind, &rest)) != KATHERINE_E_OK) {
        goto err_address;
    }

    if (control->ops == NULL || control->kind != kind) {
        res = KATHERINE_E_INVAL;
        goto err_control;
    }

    switch (kind) {
#ifdef KATHERINE_FEATURE_UDP
    case KATHERINE_TRANSPORT_UDP: {
        const katherine_udp_config_t session = {
            .local_port     = local_udp_port,
            .remote_address = rest,
            .remote_port    = KATHERINE_UDP_REMOTE_PORT,
            .timeout_ms     = KATHERINE_UDP_DATA_TIMEOUT,
            .pin_remote     = true,
            .count_drops    = true,
            .buffer_size    = md_buffer_size,
        };

        res = katherine_transport_init_udp(data, &session);
        if (res == KATHERINE_E_OK && actual_buffer_size != NULL) {
            // A UDP session serves the size it was asked for exactly, so
            // the hint and the answer only differ where the hint was zero.
            *actual_buffer_size = md_buffer_size == 0 ? KATHERINE_UDP_DEFAULT_BUFFER_SIZE : md_buffer_size;
        }
        break;
    }
#endif /* KATHERINE_FEATURE_UDP */

#ifdef KATHERINE_FEATURE_USB
    case KATHERINE_TRANSPORT_USB: {
        katherine_usb_config_t config = katherine_usb_config_default();

        if ((res = katherine_usb_config_set_address(&config, address)) != KATHERINE_E_OK) break;
        if ((res = katherine_usb_config_borrow_device(&config, control)) != KATHERINE_E_OK) break;

        // The hint names the whole buffer, the same quantity it names on
        // UDP, so a ring spends it across its slots rather than giving each
        // one that much. Undivided, a size chosen for UDP asks for queue_len
        // times what the caller meant and the open is refused for want of
        // memory.
        const uint32_t slots = (config.stream == KATHERINE_USB_STREAM_ASYNC && config.queue_len != 0)
            ? config.queue_len
            : 1;

        if (md_buffer_size != 0) {
            // Whole frames: the streamer emits them whole, and a slot ending
            // mid-frame would split one across two reads.
            size_t per_slot = md_buffer_size / slots;
            per_slot -= per_slot % KATHERINE_USB_DATA_FRAME_SIZE;
            if (per_slot == 0) per_slot = KATHERINE_USB_DATA_FRAME_SIZE;
            if (per_slot > (size_t) UINT32_MAX) {
                res = KATHERINE_E_INVAL;
                break;
            }

            config.buffer_size = (uint32_t) per_slot;
        }

        res = katherine_transport_init_usb(data, &config);
        if (res == KATHERINE_E_OK && actual_buffer_size != NULL) {
            // What the ring holds altogether, which is what the hint named
            // and what a truncated datagram is counted against.
            *actual_buffer_size = (size_t) config.buffer_size * slots;
        }
        break;
    }
#endif /* KATHERINE_FEATURE_USB */

    default:
        res = KATHERINE_E_UNSUPPORTED;
        break;
    }

err_control:
err_address:
err_args:
    return res;
}

#ifdef __cplusplus
}
#endif

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
