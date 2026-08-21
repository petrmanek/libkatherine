/**
 * @file
 * @brief Implementation of Katherine device communication.
 * @author Petr Mánek
 * @date 14.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <katherine/device.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS

static const uint16_t CONTROL_PORT = 1555;
static const uint16_t DATA_PORT    = 1556;
static const uint16_t REMOTE_PORT  = 1555;

static const uint32_t CONTROL_TIMEOUT = 100; // ms
static const uint32_t DATA_TIMEOUT    = 100; // ms

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Initialize Katherine device.
 * @param device Katherine device
 * @param addr IP address
 * @return Error code.
 */
int
katherine_device_init(katherine_device_t *device, const char *addr)
{
    int res;

    if ((res = katherine_udp_init(&device->control_socket, CONTROL_PORT, addr, REMOTE_PORT, CONTROL_TIMEOUT)) != 0) {
        goto err_control;
    }

    // Both sessions address one readout, named here and never learned from
    // the network, so both are pinned to it right away: a client that let an
    // arriving datagram repoint its session would be hijacked for good by any
    // stray one -- a late response of a readout probed earlier, or its own
    // command delivered back to it by a loopback address nobody is bound to
    // (issue #23, "net: stop stray datagrams retargeting remote addr").
    // Pinning cannot fail and needs no undoing, so the error paths below stay
    // as they were.
    katherine_udp_pin_remote(&device->control_socket);

    if ((res = katherine_udp_init(&device->data_socket, DATA_PORT, addr, REMOTE_PORT, DATA_TIMEOUT)) != 0) {
        goto err_data;
    }

    katherine_udp_pin_remote(&device->data_socket);

    return 0;

err_data:
    katherine_udp_fini(&device->control_socket);
err_control:
    return res;
}

/**
 * Finalize Katherine device.
 * @param device Device to finalize.
 */
void
katherine_device_fini(katherine_device_t *device)
{
    katherine_udp_fini(&device->data_socket);
    katherine_udp_fini(&device->control_socket);
}
