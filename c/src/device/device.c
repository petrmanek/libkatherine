/**
 * \file
 * \brief Implementation of Katherine device communication.
 * \author Petr Mánek
 * \date 14.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>
#include <katherine/device.h>
#include <katherine/acquisition.h>
#include <katherine/status.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS

static const uint16_t CONTROL_PORT = 1555;
static const uint16_t DATA_PORT    = 1556;
static const uint16_t REMOTE_PORT  = 1555;

static const uint32_t CONTROL_TIMEOUT = 100; // ms
static const uint32_t DATA_TIMEOUT    = 100; // ms

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/// Hardware types a readout can report, and what each one is.
///
/// The recognition map is ported from the reference implementation, which is
/// the only upstream source for it. Note what that source does NOT contain: it
/// carries presentation metadata only -- code, chip name, display name, icon.
/// Every behavioural difference between generations lives in its slow-control
/// layer as method splits, not in a table. So the fields here are limited to
/// what can be stated from the map itself plus the layer counts its own
/// comments give, and nothing is asserted about how a readout behaves.
///
/// `gen` is 0 wherever the generation is not established. The reference names
/// only the Katherine readouts by generation; for HardPix, Monique, RFPix and
/// Timepix2-Lite it says nothing, and guessing would put an unverifiable
/// number in a field callers would reasonably trust.
///
/// `supported` is true for the one readout this library drives and has been
/// tested against. The rest are recognized so that an unsupported device can
/// say what it is rather than nothing at all -- which is the difference
/// between "a Katherine for Timepix3 Gen2, not supported yet" and silence.
static const katherine_device_info_t KATHERINE_DEVICE_INFO[] = {
    // clang-format off
  // hw_type  name                      chip_type            gen  max_chips supported
    {0x01,    "Katherine for Timepix3", KATHERINE_CHIP_TPX3, 1,   1,        true},
    {0x02,    "Katherine for Timepix2", KATHERINE_CHIP_TPX2, 1,   1,        false},
    {0x03,    "Katherine for Timepix3", KATHERINE_CHIP_TPX3, 2,   8,        false},
    {0x0A,    "Katherine for Timepix4", KATHERINE_CHIP_TPX4, 1,   1,        false},
    {0x20,    "HardPix for Timepix3",   KATHERINE_CHIP_TPX3, 0,   2,        false},
    {0x21,    "HardPix for Timepix2",   KATHERINE_CHIP_TPX2, 0,   2,        false},
    {0x24,    "Timepix2-Lite",          KATHERINE_CHIP_TPX2, 0,   1,        false},
    {0x25,    "Monique",                KATHERINE_CHIP_TPX3, 0,   1,        false},
    {0x26,    "RFPix",                  KATHERINE_CHIP_TPX2, 0,   1,        false},
    {0x27,    "HardPix2 for Timepix2",  KATHERINE_CHIP_TPX2, 0,   2,        false},
    // clang-format on
};

/**
 * Recognize a readout from the hardware type it reports.
 *
 * \param hw_type Hardware type as katherine_readout_status_t reports it.
 * \return What that readout is, or a structure whose hw_type is 0 if this
 *   version does not know the type.
 */
katherine_device_info_t
katherine_device_info_recognize(uint8_t hw_type)
{
    const size_t n = sizeof(KATHERINE_DEVICE_INFO) / sizeof(KATHERINE_DEVICE_INFO[0]);

    // Match the hardware type in O(n)
    for (size_t i = 0; i < n; ++i) {
        if (KATHERINE_DEVICE_INFO[i].hw_type == hw_type) return KATHERINE_DEVICE_INFO[i];
    }

    // Deliberately not an error: an unknown readout is a readout this version
    // predates, and reporting hw_type 0 lets a caller say so.
    const katherine_device_info_t unknown = {0};
    return unknown;
}

/**
 * Initialize Katherine device.
 * \param device Katherine device
 * \param addr IP address
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDR if the given address is not a valid IPv4 address,
 *   or the control or data socket's fixed local port could not be bound;
 *   see inet_pton(3) and bind(2).
 * \retval KATHERINE_E_IO if opening the control or data socket, or setting
 *   its options, failed at the OS level for a reason none of the other
 *   codes cover; see socket(2), setsockopt(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if opening the socket, setting its options, or
 *   initializing its mutex reported an invalid argument; see socket(2),
 *   setsockopt(2), pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if opening the socket or initializing its mutex
 *   ran out of memory; see socket(2), pthread_mutex_init(3), and
 *   katherine_udp_last_os_error(). Setting the socket options cannot produce
 *   this, setsockopt(2) documenting no ENOMEM.
 * \retval KATHERINE_E_SYSTEM if the socket's mutex could not be
 *   initialized, for a reason none of the other codes cover; see
 *   pthread_mutex_init(3) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the socket's mutex could not be
 *   initialized for lack of a non-memory system resource; see
 *   pthread_mutex_init(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_device_init(katherine_device_t *device, const char *addr)
{
    katherine_error_t res;

    // Ensure the pointer is zeroed and not garbage.
    device->acquisition = NULL;

    // Zeroed before the probe below, so a readout that never answers leaves
    // hw_type 0 rather than whatever the caller's stack held.
    memset(&device->device_info, 0, sizeof(device->device_info));
    device->fw_version = 0;

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

    // Ask the readout what it is. Deliberately not fatal: opening a device has
    // never required one to be listening, and callers rely on that -- discovery
    // and the tests both construct devices against addresses that may answer
    // nothing. A readout that does not reply leaves device_info zeroed, which
    // is the same state an unrecognized type produces, and hw_type 0 says so.
    // The user can call katherine_device_enumerate() at an arbitrary time
    // later in the future.
    // TODO: future extension point, here we could have a DEFER_ENUMERATE flag that could suppress this call
    (void) katherine_device_enumerate(device);

    return KATHERINE_E_OK;

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
 * \see katherine_acquisition_timestamp_phase_offset
 *
 * \param device Device to ask.
 * \return true if the readout corrects the phase stagger itself.
 */
bool
katherine_device_can_correct_timestamp_phase(const katherine_device_t *device)
{
    (void) device;

    return false;
}

/**
 * Finalize Katherine device.
 * \param device Device to finalize.
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

/**
 * Enumerate device by asking it about its hardware model and firmware version.
 * If the device successfully answers all calls, the found information is
 * persisted in committed in katherine_device_t, otherwise the result of the
 * previous successful enumeration is retained.
 *
 * \param device Device to enumerate.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if the status reply was not exactly the fixed
 *   response size the protocol defines.
 * \retval KATHERINE_E_STRAY if non-correlating datagrams kept arriving
 *   until the discard budget ran out before the status reply did.
 * \retval KATHERINE_E_INVAL if sending the status request, receiving its
 *   reply, or taking the control session's lock reported an invalid
 *   argument; see sendto(2), recvfrom(2), pthread_mutex_lock(3), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the status request or receiving its
 *   reply failed at the OS level for a reason none of the other codes
 *   cover; see sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the status request or receiving its
 *   reply ran out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error(). Taking the session lock cannot produce
 *   this: pthread_mutex_lock(3) does not document ENOMEM.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_device_enumerate(katherine_device_t *device)
{
    katherine_error_t res = 0;

    // Ask the device to tell us about itself.
    katherine_readout_status_t status;
    if ((res = katherine_get_readout_status(device, &status)) != 0) {
        goto err;
    }

    // Persist what we found in the device struct.
    device->device_info = katherine_device_info_recognize((uint8_t) status.hw_type);
    device->fw_version  = (uint32_t) status.fw_version;

    return KATHERINE_E_OK;

err:
    return res;
}
