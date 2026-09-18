/**
 * \file
 * \brief Functions related to readout status inquiry.
 * \author Petr Mánek
 * \date 14.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>

/**
 * \defgroup katherine_status Status
 * \ingroup katherine_c_api
 * \brief Asking a readout about its state, sensors and chips.
 */

/**
 * \addtogroup katherine_status
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
// Forward declaration, to avoid a circular include with device.h. The
// definition, and its documentation, live there.
typedef struct katherine_device katherine_device_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/** What a readout answers when asked to identify itself. */
typedef struct katherine_readout_status {
    int hw_type;          ///< Device model, determines form-factor, ports and capabilities.
    int hw_revision;      ///< Revision number within the device model line.
    int hw_serial_number; ///< Unique serial number of the unit.
    int fw_version;       ///< Version of the firmware it is running.
} katherine_readout_status_t;

KATHERINE_EXPORTED int
katherine_readout_status_snprint(char *buf, size_t cap, const katherine_readout_status_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status);

/** Link status between the readout and the ASIC. */
typedef struct katherine_comm_status {
    uint8_t comm_lines_mask; ///< Bitmap describing active output links between chip and readout, one bit per link, as the chip's own output-block channel mask reports them.
    uint32_t data_rate;      ///< Aggregate rate carried by those links, in Mb/s.
    uint8_t chip_count;      ///< How many chips passed automated digital test during readout startup. Compare against katherine_device_derived_info_t::max_chip_count for what the readout can carry.
} katherine_comm_status_t;

KATHERINE_EXPORTED int
katherine_comm_status_snprint(char *buf, size_t cap, const katherine_comm_status_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status);

/// Buffer size (in bytes) needed by katherine_get_chip_id(), inclusive of the NUL terminator.
#define KATHERINE_CHIP_ID_STR_SIZE 16

KATHERINE_EXPORTED katherine_error_t
katherine_get_chip_id(katherine_device_t *device, char *s_chip_id);

KATHERINE_EXPORTED katherine_error_t
katherine_get_readout_temperature(katherine_device_t *device, float *temperature);

KATHERINE_EXPORTED katherine_error_t
katherine_get_sensor_temperature(katherine_device_t *device, float *temperature);

KATHERINE_EXPORTED katherine_error_t
katherine_perform_digital_test(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage);

#ifdef __cplusplus
}
#endif

/** \} */
