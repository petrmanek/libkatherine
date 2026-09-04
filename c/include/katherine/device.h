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

/// Timepix chip a readout device drives. Values are the Timepix generation, so
/// the enumeration reads as the chip's name; 0 means not known.
///
/// Named for the chip rather than for the ASIC: an ASIC is the technology a
/// chip is built in, the way an FPGA or a CPU is, while the chip is the thing
/// a readout carries and addresses -- which is why this pairs with
/// katherine_device_info_t::max_chip_count and
/// katherine_comm_status_t::chip_count.
typedef enum katherine_chip_type {
    KATHERINE_CHIP_UNKNOWN = 0, ///< Invalid value
    KATHERINE_CHIP_TPX2    = 2, ///< Timepix2
    KATHERINE_CHIP_TPX3    = 3, ///< Timepix3
    KATHERINE_CHIP_TPX4    = 4, ///< Timepix4
} katherine_chip_type_t;

KATHERINE_EXPORTED const char *
katherine_str_chip_type(katherine_chip_type_t v);


/// What a readout is, recognized from the hardware type it reports.
typedef struct katherine_device_info {
    uint8_t hw_type;                 ///< Reported hardware type. 0 means this structure is not populated: either the readout did not answer or it reported a type this version does not know.
    const char *name;                ///< Human-readable readout name, or NULL when hw_type is 0.
    katherine_chip_type_t chip_type; ///< Timepix chip this readout drives.
    uint8_t gen;                     ///< Katherine readout generation (indexed from 1), or 0 where it is not applicable.
    uint8_t max_chip_count;          ///< Maximum number of chips this readout can carry. Caution: this does not give the number of chips attached at the moment, see katherine_comm_status_t::chip_count for that.
    bool supported;                  ///< Whether this library can drive this readout today. False rows are recognized so that an unsupported device reports what it is instead of nothing.
} katherine_device_info_t;

KATHERINE_EXPORTED int
katherine_device_info_snprint(char *buf, size_t cap, const katherine_device_info_t *v);

KATHERINE_EXPORTED katherine_device_info_t
katherine_device_info_recognize(uint8_t hw_type);


typedef struct katherine_device {
    /// Slow control communication channel, which carries commands and
    /// acknowledgements (full duplex).
    katherine_udp_t control_socket;

    /// Measurement data (MD) communication channel, only used during
    /// acquisition (half duplex towards this system).
    katherine_udp_t data_socket;

    /// The acquisition measuring on this device, or NULL when none is. Opaque
    /// by design. Only kept for bookkeeping purposes.
    void *acquisition;

    /// What this readout is, recognized during katherine_device_init(). Can
    /// be repeated by katherine_device_enumerate(). Check hw_type against 0
    /// before reading the rest.
    katherine_device_info_t device_info;

    /// Firmware version reported during katherine_device_init(), or 0 if the
    /// readout did not answer.
    uint32_t fw_version;
} katherine_device_t;

KATHERINE_EXPORTED int
katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_device_init(katherine_device_t *device, const char *addr);

KATHERINE_EXPORTED void
katherine_device_fini(katherine_device_t *device);

KATHERINE_EXPORTED bool
katherine_device_can_correct_timestamp_phase(const katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_device_enumerate(katherine_device_t *device);

#ifdef __cplusplus
}
#endif

/** \} */
