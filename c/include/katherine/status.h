/**
 * @file
 * @brief Functions related to readout status inquiry.
 * @author Petr Mánek
 * @date 14.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <katherine/global.h>

/**
 * @addtogroup c_api
 * @{
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
katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status);

typedef struct katherine_comm_status {
    uint8_t comm_lines_mask;
    uint32_t data_rate;
    bool chip_detected;
} katherine_comm_status_t;

KATHERINE_EXPORTED int
katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status);

#define KATHERINE_CHIP_ID_STR_SIZE 16

KATHERINE_EXPORTED int
katherine_get_chip_id(katherine_device_t *device, char *s_chip_id);

KATHERINE_EXPORTED int
katherine_get_readout_temperature(katherine_device_t *device, float *temperature);

KATHERINE_EXPORTED int
katherine_get_sensor_temperature(katherine_device_t *device, float *temperature);

KATHERINE_EXPORTED int
katherine_perform_digital_test(katherine_device_t *device);

KATHERINE_EXPORTED int
katherine_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage);

#ifdef __cplusplus
}
#endif

/** @} */
