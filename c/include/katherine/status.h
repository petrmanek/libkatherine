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
 * \addtogroup c_api
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_device katherine_device_t;

typedef struct katherine_readout_status {
    int hw_type;
    int hw_revision;
    int hw_serial_number;
    int fw_version;
} katherine_readout_status_t;

KATHERINE_EXPORTED int
katherine_readout_status_snprint(char *buf, size_t cap, const katherine_readout_status_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status);

/// Link status between the readout and the ASIC.
typedef struct katherine_comm_status {
    /// Active sensor output links, one bit per link, as the sensor's own
    /// output-block channel mask reports them.
    uint8_t comm_lines_mask;

    /// Aggregate rate carried by those links, in Mb/s.
    uint32_t data_rate;

    /// ASICs found to be answering on those links. The readout reports a count
    /// in an eight-bit field; libkatherine 1.x truncated it to a boolean, which
    /// loses how many of a multi-chip readout's layers are actually populated.
    /// Compare against katherine_device_info_t::max_chip_count, which says how
    /// many the hardware can drive.
    uint8_t chip_count;
} katherine_comm_status_t;

KATHERINE_EXPORTED int
katherine_comm_status_snprint(char *buf, size_t cap, const katherine_comm_status_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status);

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
