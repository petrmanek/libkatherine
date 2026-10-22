/**
 * \file
 * \brief Functions related to Katherine.
 * \author Petr Mánek
 * \date 14.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/transport.h>

/**
 * \defgroup katherine_device Device
 * \ingroup katherine_c_api
 * \brief Opening a readout, learning what it is, and closing it again.
 */

/**
 * \addtogroup katherine_device
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Buffer an address needs, counting the terminator. Long enough for the
 * longest form a transport writes: a scheme, "://", a bus number and a chain
 * of up to seven USB port numbers.
 */
#define KATHERINE_DEVICE_ADDRESS_STR_SIZE 64

/**
 * One readout that answered a discovery.
 *
 * Common to every link: what differs is how a transport goes looking, not
 * what it finds. See katherine_device_discover_udp() and
 * katherine_device_discover_usb().
 */
typedef struct katherine_discovered {
    char address[KATHERINE_DEVICE_ADDRESS_STR_SIZE]; ///< Address it answered from, naming the link too: "udp://192.168.1.42" or "usb://4-1". Ready to hand to katherine_device_init().
    bool legacy;                                     ///< True if the firmware ignored the reply port and answered on the standard one. UDP's; false on a link with no such notion.
} katherine_discovered_t;

/**
 * Lowest port a readout will answer a discovery on. The firmware reads the
 * reply port out of the request's first two bytes and falls back to its own
 * default below this, which would send every answer to the port a running
 * session is already using.
 */
#define KATHERINE_DISCOVER_MIN_PORT 32

#ifdef KATHERINE_FEATURE_UDP
KATHERINE_EXPORTED katherine_error_t
katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count);
#endif /* KATHERINE_FEATURE_UDP */

#ifdef KATHERINE_FEATURE_USB
KATHERINE_EXPORTED katherine_error_t
katherine_device_discover_usb(katherine_discovered_t *out, size_t *count);
#endif /* KATHERINE_FEATURE_USB */

/**
 * Timepix chip a readout device drives. Values are the Timepix generation, so
 * the enumeration reads as the chip's name; 0 means not known.
 *
 * Named for the chip rather than for the ASIC: an ASIC is the technology a
 * chip is built in, the way an FPGA or a CPU is, while the chip is the thing
 * a readout carries and addresses -- which is why this pairs with
 * katherine_device_derived_info_t::max_chip_count and
 * katherine_comm_status_t::chip_count.
 */
typedef enum katherine_chip_type {
    KATHERINE_CHIP_UNKNOWN = 0, ///< Invalid value
    KATHERINE_CHIP_TPX2    = 2, ///< Timepix2
    KATHERINE_CHIP_TPX3    = 3, ///< Timepix3
    KATHERINE_CHIP_TPX4    = 4, ///< Timepix4
} katherine_chip_type_t;

KATHERINE_EXPORTED const char *
katherine_str_chip_type(katherine_chip_type_t v);


/**
 * What a readout says it is: facts it reported, or that a caller declared on
 * its behalf. Nothing here is inferred.
 *
 * Filled by katherine_device_enumerate(), which asks the readout, or by
 * katherine_device_declare(), which takes the caller's word for it. Both
 * routes then derive katherine_device_derived_info_t from hw_type, so this
 * structure is the single source of everything the library concludes about a
 * device.
 */
typedef struct katherine_device_info {
    uint8_t hw_type;        ///< Reported hardware type. 0 means nothing has been enumerated or declared yet.
    uint8_t hw_revision;    ///< Reported hardware revision.
    uint16_t serial_number; ///< Reported hardware serial number.
    uint16_t fw_version;    ///< Reported firmware version.
    uint8_t chip_count;     ///< The number of chips answering when this was taken. Compare against katherine_device_derived_info_t::max_chip_count for what the hardware could possibly carry.
    bool legacy;            ///< Whether the firmware ignores the UDP echo-port extension, and so does not honour a port change either.
} katherine_device_info_t;

KATHERINE_EXPORTED int
katherine_device_info_snprint(char *buf, size_t cap, const katherine_device_info_t *v);


/**
 * What a readout's reported hardware type means, recognized from a table.
 *
 * Derived entirely from katherine_device_info_t::hw_type, so it is never set
 * on its own: katherine_device_declare() fills it, and
 * katherine_device_enumerate() reaches it through that same call.
 */
typedef struct katherine_device_derived_info {
    const char *name;                ///< Human-readable readout name, or NULL when the hardware type is unknown.
    katherine_chip_type_t chip_type; ///< Timepix chip this readout drives.
    uint8_t gen;                     ///< Katherine readout generation (indexed from 1), or 0 where it is not applicable.
    uint8_t max_chip_count;          ///< The number of chips this readout hardware can possibly drive. Not necessarily how many are presently attached, see katherine_device_info_t::chip_count for that.
    uint8_t bias_supply_count;       ///< Bias supplies this readout provides, addressed from 0.
    uint8_t accessible_gpio_count;   ///< GPIO channels brought out where a user can reach them.
    uint8_t all_gpio_count;          ///< GPIO channels the readout has, reachable or not. At least accessible_gpio_count.
    uint8_t tdc_channel_count;       ///< The number of channels watched by the readout's timing unit, each with a counter of its own.
    uint8_t pulse_channel_count;     ///< The number of channels driven by the readout's pulse generator.
    bool supported;                  ///< Whether this library can drive this readout today. Also the enumeration guard: a device that has been neither enumerated nor declared has this false, because the structure is zeroed, and the generation-dependent calls refuse to run on it.
} katherine_device_derived_info_t;

KATHERINE_EXPORTED int
katherine_device_derived_info_snprint(char *buf, size_t cap, const katherine_device_derived_info_t *v);

KATHERINE_EXPORTED katherine_device_derived_info_t
katherine_device_derived_info_recognize(uint8_t hw_type);


/**
 * An open connection to a readout: its communication channel, what it
 * reported about itself, and what this library derives from that.
 */
typedef struct katherine_device {
    char address[KATHERINE_DEVICE_ADDRESS_STR_SIZE]; ///< Address this device was opened with, which an acquisition opens its own link from.
    katherine_transport_t control_transport;         ///< Slow control channel, used to send commands and receive acknowledgements. Owned by the device.

#ifdef KATHERINE_FEATURE_UDP
    uint16_t udp_data_port; ///< Port the readout was told to send measurement data to, which an acquisition binds. Valid only for udp:// transport.
    bool udp_ports_changed; ///< True where this device moved the readout off the standard UDP ports, so that katherine_device_fini() knows to put it back. Valid only for udp:// transport.
#endif

    void *acquisition; ///< The acquisition measuring on this device, or NULL when none is. Opaque by design.

    katherine_device_info_t info;                 ///< Self-reported information about the device, or what a caller declared. Zeroed until katherine_device_enumerate() or katherine_device_declare() has run.
    katherine_device_derived_info_t derived_info; ///< More useful attributes, automatically derived from \p info. \see katherine_device_declare
} katherine_device_t;

KATHERINE_EXPORTED int
katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v);

/**
 * Optional behaviour of katherine_device_init(), as a bitmap.
 *
 * Zero asks for everything, which is the useful default: the readout is
 * enumerated, moved to a private pair of UDP ports, and told which interface
 * it is talking over. Each flag suppresses one of those.
 *
 * All of them together open the two sockets and nothing else, sending no
 * command and waiting for no acknowledgement, so the call succeeds whether or
 * not a readout is there.
 */
typedef enum katherine_device_flags {
    KATHERINE_DEVICE_DONT_ENUMERATE        = 1u << 0, ///< Do not ask the readout what it is. Every generation-dependent call then fails with KATHERINE_E_STATE until katherine_device_enumerate() or katherine_device_declare() has run. Without this flag, a readout that does not answer fails the open.
    KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS = 1u << 1, ///< Do not move this session to a private pair of local UDP ports. Moving them is what lets one host drive several readouts, so suppress it only where something else owns the port assignment. Not attempted on a firmware that ignores the reply port, which cannot be moved; see katherine_device_info_t::legacy.
    KATHERINE_DEVICE_DONT_SELECT_INTERFACE = 1u << 2, ///< Do not tell the readout which interface drives it. On a readout that wants it, every later command answers orders of magnitude more slowly, and an acquisition over UDP does not start at all: katherine_acquisition_begin() times out. Over USB the selection changes nothing but that latency. See katherine_device_select_interface().
} katherine_device_flags_t;

KATHERINE_EXPORTED katherine_error_t
katherine_device_init(katherine_device_t *device, const char *address, uint32_t flags);

KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_device_select_interface(katherine_device_t *device);

KATHERINE_EXPORTED bool
katherine_device_can_correct_timestamp_phase(const katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_device_enumerate(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_device_declare(katherine_device_t *device, const katherine_device_info_t *info);

#ifdef __cplusplus
}
#endif

/** \} */
