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
#include <katherine/udp.h>

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
    uint8_t chip_count;     ///< Chips answering when this was taken. A snapshot, not identity: the readout reports a count while idle and a 0/1 flag while measuring, so read katherine_get_comm_status() for a live value. Compare against katherine_device_derived_info_t::max_chip_count for what the hardware could carry.
    bool legacy;            ///< Whether the firmware ignores the echo-port extension, and so does not honour a port change either.
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
    uint8_t max_chip_count;          ///< Chips this readout can carry. Not how many are attached, see katherine_device_info_t::chip_count for that.
    uint8_t bias_supply_count;       ///< Bias supplies this readout provides, addressed from 0.
    uint8_t accessible_gpio_count;   ///< GPIO channels brought out where a user can reach them.
    uint8_t all_gpio_count;          ///< GPIO channels the readout has, reachable or not. At least accessible_gpio_count.
    bool supported;                  ///< Whether this library can drive this readout today. Also the enumeration guard: a device that has been neither enumerated nor declared has this false, because the structure is zeroed, and the generation-dependent calls refuse to run on it.
} katherine_device_derived_info_t;

KATHERINE_EXPORTED int
katherine_device_derived_info_snprint(char *buf, size_t cap, const katherine_device_derived_info_t *v);

KATHERINE_EXPORTED katherine_device_derived_info_t
katherine_device_derived_info_recognize(uint8_t hw_type);


typedef struct katherine_device {
    /**
     * Slow control communication channel, which carries commands and
     * acknowledgements (full duplex).
     */
    katherine_udp_t control_socket;

    /**
     * Measurement data (MD) communication channel, only used during
     * acquisition (half duplex towards this system).
     */
    katherine_udp_t data_socket;

    /**
     * The acquisition measuring on this device, or NULL when none is. Opaque
     * by design. Only kept for bookkeeping purposes.
     */
    void *acquisition;

    /**
     * What this readout said it is, or what a caller declared. Zeroed until
     * katherine_device_enumerate() or katherine_device_declare() has run.
     */
    katherine_device_info_t info;

    /**
     * What that hardware type means. Derived from info, never set on its own.
     */
    katherine_device_derived_info_t derived_info;
} katherine_device_t;

KATHERINE_EXPORTED int
katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v);

/**
 * Optional behaviour of katherine_device_init(), as a bitmap.
 *
 * Zero asks for everything, which is the useful default: the readout is
 * enumerated, moved to a private pair of UDP ports, and told which interface
 * it is talking over. Each flag suppresses one of those.
 */
typedef enum katherine_device_flags {
    KATHERINE_DEVICE_DONT_ENUMERATE        = 1u << 0, ///< Do not ask the readout what it is. Every generation-dependent call then fails with KATHERINE_E_STATE until katherine_device_enumerate() or katherine_device_declare() has run.
    KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS = 1u << 1, ///< Do not move this session to a private pair of local UDP ports. Moving them is what lets several readouts share one network interface, so suppress this only when something else owns the port assignment.
    KATHERINE_DEVICE_DONT_SELECT_INTERFACE = 1u << 2, ///< Do not tell the readout which interface it is being driven over. On a readout that wants it, every later command answers orders of magnitude more slowly.
} katherine_device_flags_t;

KATHERINE_EXPORTED katherine_error_t
katherine_device_init(katherine_device_t *device, const char *addr, uint32_t flags);


KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *device);

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
