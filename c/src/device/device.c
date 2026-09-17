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

/**
 * One row: the hardware type a readout reports, and what it means.
 *
 * The key sits beside the derived information rather than inside it, because
 * the hardware type is something the readout said -- it belongs to
 * katherine_device_info_t -- while everything in the row's payload is this
 * table's conclusion about it.
 */
typedef struct {
    uint8_t hw_type;                         ///< Key: the reported hardware type
    katherine_device_derived_info_t derived; ///< What that type means
} katherine_device_row_t;

/**
 * Hardware types a readout can report, and what each one is.
 *
 * The recognition map is ported from the reference implementation, which is
 * the only upstream source for it. Note what that source does NOT contain: it
 * carries presentation metadata only -- code, chip name, display name, icon.
 * Every behavioural difference between generations lives in its slow-control
 * layer as method splits, not in a table. So the fields here are limited to
 * what can be stated from the map itself plus the layer counts its own
 * comments give, and nothing is asserted about how a readout behaves.
 *
 * `gen` is established for every row. The reference names only the Katherine
 * readouts by generation; the rest are Petr's, 2026-09-17 -- HardPix for
 * Timepix3 and for Timepix2, Monique, Timepix2-Lite and RFPix are all
 * generation 1, and HardPix2 is generation 2. It matters beyond bookkeeping,
 * because the generation is what selects a measurement-data header map: a
 * HardPix2 decoded as generation 1 would lose every pixel.
 *
 * The capability counts are filled only where they have been checked, and are
 * 0 otherwise. Zero reads as unknown rather than as none, which is why
 * `bias_supply_count` should not be used to decide whether a readout has a
 * bias supply at all.
 *
 * `supported` is the flag the generation-dependent calls test before they
 * run, so a row claiming it enables the acquisition-time encoding and the
 * measurement-data header map for that hardware. True for the four Timepix3
 * readouts this library drives: the Gen1 and Gen2 Katherines, both measured
 * here, plus HardPix for Timepix3 and Monique, which Petr states are drivable
 * (2026-09-17) and which this project has no sample of. The rest are
 * recognized so that an undrivable device can say what it is rather than
 * nothing at all -- the difference between "a Katherine for Timepix2, not
 * supported yet" and silence.
 */

static const katherine_device_row_t KATHERINE_DEVICE_INFO[] = {
    // clang-format off
  // hw_type    name                      chip_type            gen  chips  bias  gpio: acc/all  supported
    {0x01,    {"Katherine for Timepix3", KATHERINE_CHIP_TPX3, 1,   1,     1,    4,    4,       true}},
    {0x02,    {"Katherine for Timepix2", KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       false}},
    {0x03,    {"Katherine for Timepix3", KATHERINE_CHIP_TPX3, 2,   8,     2,    4,    8,       true}},
    {0x0A,    {"Katherine for Timepix4", KATHERINE_CHIP_TPX4, 1,   1,     0,    0,    0,       false}},
    {0x20,    {"HardPix for Timepix3",   KATHERINE_CHIP_TPX3, 1,   2,     0,    0,    0,       true}},
    {0x21,    {"HardPix for Timepix2",   KATHERINE_CHIP_TPX2, 1,   2,     0,    0,    0,       false}},
    {0x24,    {"Timepix2-Lite",          KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       false}},
    {0x25,    {"Monique",                KATHERINE_CHIP_TPX3, 1,   1,     0,    0,    0,       true}},
    {0x26,    {"RFPix",                  KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       false}},
    {0x27,    {"HardPix2 for Timepix2",  KATHERINE_CHIP_TPX2, 2,   2,     0,    0,    0,       false}},
    // clang-format on
};

/**
 * Recognize what a reported hardware type means.
 *
 * \param hw_type Hardware type as katherine_readout_status_t reports it.
 * \return What that readout is, or a zeroed structure if this version does
 *   not know the type. A zeroed one has supported false, which is what makes
 *   the generation-dependent calls refuse an unrecognized readout on exactly
 *   the same test they use to refuse an un-enumerated one.
 */
katherine_device_derived_info_t
katherine_device_derived_info_recognize(uint8_t hw_type)
{
    const size_t n = sizeof(KATHERINE_DEVICE_INFO) / sizeof(KATHERINE_DEVICE_INFO[0]);

    // Match the hardware type in O(n)
    for (size_t i = 0; i < n; ++i) {
        if (KATHERINE_DEVICE_INFO[i].hw_type == hw_type) return KATHERINE_DEVICE_INFO[i].derived;
    }

    // Deliberately not an error: an unknown readout is a readout this version
    // predates, and a zeroed structure lets a caller say so.
    const katherine_device_derived_info_t unknown = {0};
    return unknown;
}


/**
 * Initialize Katherine device.
 * \param device Katherine device
 * \param addr IP address
 * \param flags Zero for the full open, or katherine_device_flags_t values
 *   suppressing individual steps of it
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
katherine_device_init(katherine_device_t *device, const char *addr, uint32_t flags)
{
    katherine_error_t res;

    // Ensure the pointer is zeroed and not garbage.
    device->acquisition = NULL;

    // Zeroed before the probe below, so a readout that never answers leaves
    // hw_type 0 rather than whatever the caller's stack held.
    memset(&device->info, 0, sizeof(device->info));
    memset(&device->derived_info, 0, sizeof(device->derived_info));

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

    // Optional initial setup
    if ((flags & KATHERINE_DEVICE_DONT_ENUMERATE) == 0) {
        (void) katherine_device_enumerate(device);
    }

    (void) (flags & KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS); // TODO
    (void) (flags & KATHERINE_DEVICE_DONT_SELECT_INTERFACE); // TODO

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
 * Take the caller's word for what a readout is, instead of asking it.
 *
 * The one writer of both structures. katherine_device_enumerate() asks the
 * readout and then comes here, so there is a single path from a hardware type
 * to everything the library concludes from it, and no second route that could
 * derive it differently.
 *
 * \param device Device to describe
 * \param info What the readout is; the caller owns being right
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if info is NULL.
 */
katherine_error_t
katherine_device_declare(katherine_device_t *device, const katherine_device_info_t *info)
{
    katherine_error_t res = 0;

    if (info == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_info_null;
    }

    device->info         = *info;
    device->derived_info = katherine_device_derived_info_recognize(info->hw_type);

    return KATHERINE_E_OK;

err_info_null:
    return res;
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

    // Send a couple of probing commands.
    katherine_readout_status_t status = {0};
    if ((res = katherine_get_readout_status(device, &status)) != 0) {
        goto err_readout_status;
    }

    katherine_comm_status_t comm = {0};
    if ((res = katherine_get_comm_status(device, &comm)) != 0) {
        goto err_comm_status;
    }

    // TODO: probe capability to redirect ports

    // Collect all information
    const katherine_device_info_t info = {
        .hw_type       = (uint8_t) status.hw_type,
        .hw_revision   = (uint8_t) status.hw_revision,
        .serial_number = (uint16_t) status.hw_serial_number,
        .fw_version    = (uint16_t) status.fw_version,
        .chip_count    = comm.chip_count,
        .legacy        = false, // FIXME: hard-coded, need echo-port extension for this
    };

    if ((res = katherine_device_declare(device, &info)) != 0) {
        goto err_device_declare;
    }

    return KATHERINE_E_OK;

err_device_declare:
err_comm_status:
err_readout_status:
    return res;
}
