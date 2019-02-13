//
// Created by petr on 14.6.18.
//

#pragma once

/**
 * @file
 * @brief Functions related to readout status inquiry.
 */

#include <stdint.h>
#include <stdbool.h>
#include <katherine/global.h>

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
katherine_get_readout_status(katherine_device_t *, katherine_readout_status_t *);

typedef struct katherine_comm_status {
    uint8_t comm_lines_mask;
    uint32_t data_rate;
    bool chip_detected;
} katherine_comm_status_t;

KATHERINE_EXPORTED int
katherine_get_comm_status(katherine_device_t *, katherine_comm_status_t *);

#define KATHERINE_CHIP_ID_STR_SIZE 16

KATHERINE_EXPORTED int
katherine_get_chip_id(katherine_device_t *, char *);

KATHERINE_EXPORTED int
katherine_get_readout_temperature(katherine_device_t *, float *);

KATHERINE_EXPORTED int
katherine_get_sensor_temperature(katherine_device_t *, float *);

KATHERINE_EXPORTED int
katherine_perform_digital_test(katherine_device_t *);

KATHERINE_EXPORTED int
katherine_get_adc_voltage(katherine_device_t *, unsigned char, float *);

#ifdef __cplusplus
}
#endif
