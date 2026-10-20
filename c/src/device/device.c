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
#include <katherine/error.h>
#include "../transport/udp.h"
#include <katherine/acquisition.h>
#include <katherine/status.h>
#include "../address.h"
#include "../protocol/cmd_interface.h"
#include "../transport/factory.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// What the legacy probe waits for an answer. Its own rather than a session's:
// it runs before any transport exists, and what a broadcast probe should wait
// is not what a command should.
static const uint32_t DISCOVER_TIMEOUT = 100; // ms

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
  // hw_type    name                      chip_type            gen  chips  bias  gpio: acc/all  tdc  pulse  supported
    {0x01,    {"Katherine for Timepix3", KATHERINE_CHIP_TPX3, 1,   1,     1,    4,    4,       3,   0,     true}},
    {0x02,    {"Katherine for Timepix2", KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       0,   0,     false}},
    {0x03,    {"Katherine for Timepix3", KATHERINE_CHIP_TPX3, 2,   8,     2,    4,    8,       5,   4,     true}},
    {0x0A,    {"Katherine for Timepix4", KATHERINE_CHIP_TPX4, 1,   1,     0,    0,    0,       0,   0,     false}},
    {0x20,    {"HardPix for Timepix3",   KATHERINE_CHIP_TPX3, 1,   2,     2,    0,    0,       0,   0,     true}},
    {0x21,    {"HardPix for Timepix2",   KATHERINE_CHIP_TPX2, 1,   2,     0,    0,    0,       0,   0,     false}},
    {0x24,    {"Timepix2-Lite",          KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       0,   0,     false}},
    {0x25,    {"Monique",                KATHERINE_CHIP_TPX3, 1,   1,     1,    0,    0,       0,   0,     true}},
    {0x26,    {"RFPix",                  KATHERINE_CHIP_TPX2, 1,   1,     0,    0,    0,       0,   0,     false}},
    {0x27,    {"HardPix2 for Timepix2",  KATHERINE_CHIP_TPX2, 2,   2,     0,    0,    0,       0,   0,     false}},
    // clang-format on
};

/**
 * Recognize what a reported hardware type means.
 *
 * \param hw_type Hardware type as katherine_readout_status_t reports it.
 * \return What that readout is, or a zeroed structure if this version does not
 *   know the type. A zeroed one has supported false, which is what makes the
 *   generation-dependent calls refuse an unrecognized readout on exactly the
 *   same test they use to refuse an un-enumerated one.
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
 * Pin the readout to the link that is driving it.
 *
 * A readout that has not been told polls every interface it has, which adds
 * latency to every command it answers -- about 69 ms against 0.14 ms on a
 * second-generation readout. The choice holds until it is restarted.
 *
 * Which link to name is not a question for the caller: it is the one this
 * device was opened over.
 *
 * katherine_device_init() does this at open, so there is normally nothing to
 * call. It is for a caller that passed KATHERINE_DEVICE_DONT_SELECT_INTERFACE
 * and wants it later.
 *
 * Safe on a readout that has no such command. Both first-generation firmwares
 * send the opcode to the same do-nothing exit as any unknown one, so the send
 * succeeds and selects nothing.
 *
 * \see katherine_device_init
 * \see KATHERINE_DEVICE_DONT_SELECT_INTERFACE
 *
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success. The command is unacknowledged, so this
 *   says the datagram left, not that the readout acted on it.
 * \retval KATHERINE_E_UNSUPPORTED if the link this device was opened over has
 *   no encoding the readout would recognize.
 * \retval KATHERINE_E_INVAL if the send or the lock rejected an argument; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the send would have blocked.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason none
 *   of the other codes cover; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_last_os_error().
 */
katherine_error_t
katherine_device_select_interface(katherine_device_t *device)
{
    // What the readout reads for each link, in its own encoding. A kind with no
    // entry here is one whose byte is not known yet.
    static const uint8_t INTERFACE_BYTE[] = {
        [KATHERINE_TRANSPORT_UDP] = 0,
        [KATHERINE_TRANSPORT_USB] = 1,
    };

    const size_t n                        = sizeof(INTERFACE_BYTE) / sizeof(INTERFACE_BYTE[0]);
    const katherine_transport_kind_t kind = device->control_transport.kind;

    katherine_error_t res;

    if ((size_t) kind >= n) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_kind;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    // Unacknowledged, so nothing waits for a reply.
    res = katherine_cmd_send60(&device->control_transport, CMD_TYPE_INTERFACE_SELECTION, INTERFACE_BYTE[kind]);
    if (res) goto err_send;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_kind:
    return res;
}

// Tell the readout to answer on the ports the sessions actually got, which
// is what frees the standard pair for somebody else.
static katherine_error_t
claim_private_ports(katherine_device_t *device)
{
    katherine_error_t res;
    uint16_t control;

    res = katherine_udp_transport_bound_port(&device->control_transport, &control);
    if (res) goto err_control;

    // The one after the control port: nothing binds the data socket until an
    // acquisition asks, so a number has to be named before one exists.
    const uint16_t data = (uint16_t) (control + 1);

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    res = katherine_cmd_change_udp_ports(&device->control_transport, control, data);

    (void) katherine_transport_mutex_unlock(&device->control_transport);

    if (res == KATHERINE_E_OK) device->udp_data_port = data;

err_lock:
err_control:
    return res;
}

// Only where this device moved them. A readout whose firmware ignores the
// reply port was never moved and must not be told anything, and neither must
// one a caller asked to leave alone.
//
// Best effort otherwise: a readout can recover itself, and neither caller
// here has anywhere to report a failure.
static void
restore_standard_ports(katherine_device_t *device)
{
    if (!device->udp_ports_changed) return;

    device->udp_ports_changed = false;

    if (katherine_transport_mutex_lock(&device->control_transport) != KATHERINE_E_OK) return;

    (void) katherine_cmd_change_udp_ports(&device->control_transport, KATHERINE_UDP_CONTROL_PORT, KATHERINE_UDP_DATA_PORT);
    (void) katherine_transport_mutex_unlock(&device->control_transport);
}

/**
 * Initialize Katherine device.
 * \param device Katherine device
 * \param address Address of the readout, naming the link as well as the readout
 *   on it: "udp://192.168.1.42". An address with no scheme is a UDP address,
 *   which is what every address written before 2.0 is.
 * \param flags Zero for the full open, or katherine_device_flags_t values
 *   suppressing individual steps of it
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDRESS if the address names a link this library does not
 *   know, or is a scheme and nothing else, or its UDP part is not a valid
 *   IPv4 address, or the control or data socket's fixed local port could not
 *   be bound; see inet_pton(3) and bind(2).
 * \retval KATHERINE_E_UNSUPPORTED if the address names a link this version
 *   cannot open a device on. Only UDP can carry one so far.
 * \retval KATHERINE_E_BAD_CRD if the readout answered the port change or the
 *   enumeration with something that was not the length of a command response.
 * \retval KATHERINE_E_STRAY if responses to other commands kept arriving
 *   until the discard budget ran out before the port change or the
 *   enumeration was answered.
 * \retval KATHERINE_E_IO if opening the control or data socket, setting its
 *   options, or telling the readout which interface drives it failed at the OS
 *   level for a reason none of the other codes cover; see socket(2),
 *   setsockopt(2) and katherine_udp_last_os_error() for the sockets this call
 *   opens, and katherine_transport_last_os_error() for the command it sends.
 * \retval KATHERINE_E_INVAL if opening the socket, setting its options, or
 *   initializing its mutex reported an invalid argument; see socket(2),
 *   setsockopt(2), pthread_mutex_init(3), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if opening the socket or initializing its mutex
 *   ran out of memory; see socket(2), pthread_mutex_init(3), and
 *   katherine_udp_last_os_error(). Setting the socket options cannot produce
 *   this, setsockopt(2) documenting no ENOMEM.
 * \retval KATHERINE_E_SYSTEM if the socket's mutex could not be initialized,
 *   for a reason none of the other codes cover; see pthread_mutex_init(3) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the enumeration
 *   within the control session's receive timeout, if the socket's mutex could
 *   not be initialized for lack of a non-memory system resource, or if telling
 *   the readout which interface drives it would have blocked; see
 *   pthread_mutex_init(3) and katherine_udp_last_os_error() for the mutex, and
 *   katherine_transport_last_os_error() for the command.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \note Unreachable here: KATHERINE_E_STATE.
 *   katherine_device_select_interface() returns it, but this call makes it
 *   only on a readout already known to be supported and of the second
 *   generation.
 */
katherine_error_t
katherine_device_init(katherine_device_t *device, const char *address, uint32_t flags)
{
    katherine_error_t res;

    // Ensure the pointer is zeroed and not garbage.
    device->acquisition       = NULL;
    device->udp_ports_changed = false;
    device->udp_data_port     = KATHERINE_UDP_DATA_PORT;

    // An acquisition opens its own link from this, after the caller's string
    // may be gone.
    if (address == NULL || strlen(address) >= sizeof(device->address)) {
        return KATHERINE_E_ADDRESS;
    }
    strcpy(device->address, address);

    // Zeroed before the probe below, so a readout that never answers leaves
    // hw_type 0 rather than whatever the caller's stack held.
    memset(&device->info, 0, sizeof(device->info));
    memset(&device->derived_info, 0, sizeof(device->derived_info));

    // The address names the link as well as the readout, and only the part
    // after the scheme is the transport's to read.
    katherine_transport_kind_t kind;
    const char *host;
    if ((res = katherine_address_split(address, &kind, &host)) != KATHERINE_E_OK) {
        goto err_address;
    }
    // Before the main control and data channels are open, run a quick check that
    // determines whether a readout is a legacy firmware or not. This requires the
    // main control port to be 1555, which would clash with the default control
    // socket.
    //
    // UDP's alone: the flag records whether a firmware honoured the reply port
    // of a discovery datagram, and a link that is not a shared network has no
    // such thing.
    //
    // True where nothing answered, because the flag gates a port change the
    // first generation cannot honour: guessing false there would issue it to a
    // readout that drops it.
    bool legacy = true;

    if (kind == KATHERINE_TRANSPORT_UDP && (flags & KATHERINE_DEVICE_DONT_ENUMERATE) == 0) {
        katherine_discovered_t found;
        size_t count = 1;

        if (katherine_device_discover_udp(host, KATHERINE_UDP_DATA_PORT, DISCOVER_TIMEOUT, &found, &count) == KATHERINE_E_OK
            && count == 1) {
            legacy = found.legacy;
        }
    }

    // A firmware that ignores the reply port cannot be moved off the standard
    // pair, and a caller may have its own reason to stay there.
    const bool should_change_ports = kind == KATHERINE_TRANSPORT_UDP && (flags & KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS) == 0 && !legacy;

    // Before katherine_device_enumerate() below, which is the first thing
    // here that takes the control lock.
    if ((res = katherine_transport_init_for_control(address, should_change_ports, &device->control_transport)) != KATHERINE_E_OK) {
        goto err_control;
    }

    // Optional initial setup:
    // 1. Change ports if supported and not inhibited by flags
    if (should_change_ports) {
        if ((res = claim_private_ports(device)) != KATHERINE_E_OK) {
            goto err_ports;
        }

        device->udp_ports_changed = true;
    }

    // 2. Gen2 answers orders of magnitude faster once it has been told which
    //    interface drives it, so this goes before anything that waits.
    if ((flags & KATHERINE_DEVICE_DONT_SELECT_INTERFACE) == 0) {
        if ((res = katherine_device_select_interface(device)) != KATHERINE_E_OK) {
            goto err_select;
        }
    }

    // 3. Discover device capabilities if not inhibited by flags
    if ((flags & KATHERINE_DEVICE_DONT_ENUMERATE) == 0) {
        if ((res = katherine_device_enumerate(device)) != KATHERINE_E_OK) {
            goto err_enumerate;
        }

        // Propagate the result of the legacy check into the device info.
        if (kind == KATHERINE_TRANSPORT_UDP) device->info.legacy = legacy;
    }

    return KATHERINE_E_OK;

err_enumerate:
err_select:
    restore_standard_ports(device);
err_ports:
    katherine_transport_fini(&device->control_transport);
err_control:
err_address:
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

    // Put the readout back on the standard pair, or the next client finds it
    // apparently dead.
    if (device->control_transport.ops != NULL) restore_standard_ports(device);

    // Before the session it drives, which is the order it was opened in, and
    // only where there is one: a device assembled by hand rather than by
    // katherine_device_init() carries a zeroed transport, and whether this
    // device opened one is the device's own bookkeeping rather than
    // something the transport layer should be asked to tolerate.
    // Reverse order of acquisition, and each only where there is one: a device
    // assembled by hand carries a zeroed transport. A link whose data
    // transport drives what its control transport claimed needs this order,
    // the claim outliving what points into it.
    if (device->control_transport.ops != NULL) katherine_transport_fini(&device->control_transport);
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
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the control
 *   link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if the status reply was not exactly the fixed
 *   response size the protocol defines.
 * \retval KATHERINE_E_STRAY if non-correlating datagrams kept arriving until
 *   the discard budget ran out before the status reply did.
 * \retval KATHERINE_E_INVAL if sending the status request, receiving its
 *   reply, or taking the control link's lock reported an invalid argument; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if sending the status request or receiving its reply
 *   failed at the OS level for a reason none of the other codes cover; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the status request or receiving its
 *   reply ran out of memory; see katherine_transport_last_os_error(). Taking
 *   the control link's lock cannot produce this through the UDP transport:
 *   pthread_mutex_lock(3) does not document ENOMEM.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_last_os_error().
 */
katherine_error_t
katherine_device_enumerate(katherine_device_t *device)
{
    katherine_error_t res = 0;

    // Send a couple of probing commands.
    katherine_readout_status_t status = {0};
    if ((res = katherine_get_readout_status(device, &status)) != KATHERINE_E_OK) {
        goto err_readout_status;
    }

    katherine_comm_status_t comm = {0};
    if ((res = katherine_get_comm_status(device, &comm)) != KATHERINE_E_OK) {
        goto err_comm_status;
    }

    // Collect all information
    const katherine_device_info_t info = {
        .hw_type       = (uint8_t) status.hw_type,
        .hw_revision   = (uint8_t) status.hw_revision,
        .serial_number = (uint16_t) status.hw_serial_number,
        .fw_version    = (uint16_t) status.fw_version,
        .chip_count    = comm.chip_count,
        // legacy is not in anything the readout answers; katherine_device_init()
        // learns it from discovery and writes it after this call.
    };

    if ((res = katherine_device_declare(device, &info)) != KATHERINE_E_OK) {
        goto err_device_declare;
    }

    return KATHERINE_E_OK;

err_device_declare:
err_comm_status:
err_readout_status:
    return res;
}
