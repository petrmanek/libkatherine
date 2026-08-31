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
#include <katherine/acquisition.h>

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

    // Ensure the pointer is zeroed and not garbage.
    device->acquisition = NULL;

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
 * Whether this readout can apply per-double-column phase correction itself.
 *
 * The pixel clock reaches the double columns in staggered phases, and from
 * some firmware revision onward the readout can subtract that stagger before
 * sending, sparing the host the work. The capability therefore depends on the
 * firmware version as much as on the model, which is why the question is asked
 * of a device rather than of a hardware-type row: only the device carries both.
 *
 * False throughout for now. Wiring it up needs the opcode and its minimum
 * firmware version identified, and the version retained at open, where it is
 * currently read and discarded.
 *
 * @see katherine_acquisition_timestamp_phase_offset
 *
 * @param device Device to ask.
 * @return true if the readout corrects the phase stagger itself.
 */
bool
katherine_device_can_correct_timestamp_phase(const katherine_device_t *device)
{
    (void) device;

    return false;
}

/**
 * Finalize Katherine device.
 * @param device Device to finalize.
 */
void
katherine_device_fini(katherine_device_t *device)
{
    // Last-ditch failsafe: if an acquisition has been started by this point and forgotten, abort and disown it.
    if (device->acquisition != NULL) {
        katherine_acquisition_t *acq = (katherine_acquisition_t *) device->acquisition;
        (void) katherine_acquisition_abort(acq);
        device->acquisition = NULL;
    }

    katherine_udp_fini(&device->data_socket);
    katherine_udp_fini(&device->control_socket);
}
