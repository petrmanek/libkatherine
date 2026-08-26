/**
 * @file
 * @brief Implementation of readout status inquiry.
 * @author Petr Mánek
 * @date 14.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <katherine/status.h>
#include <katherine/error.h>
#include <katherine/global.h>
#include <katherine/device.h>
#include "protocol/cmd_interface.h"
#include "protocol/crd.h"

/**
 * Inquire the status of the readout.
 * @param device Katherine device
 * @param status Retrieved status information
 * @return Error code.
 */
int
katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    /* Every inquiry below opens the same way: the session is flushed before
       the command goes out, so a response an earlier exchange left behind
       cannot be read as this one's. Correlation catches a response
       identified as some other command's; only the flush catches a stale
       response of the *same* command, which would correlate perfectly. */
    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_readout_status(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_READOUT_STATUS, crd);
    if (res) goto err;

    const uint64_t *status_crd = (const uint64_t *) &crd;
    status->hw_type            = EXTRACT(*status_crd, readout_status_crd, hw_type);
    status->hw_revision        = EXTRACT(*status_crd, readout_status_crd, hw_revision);
    status->hw_serial_number   = EXTRACT(*status_crd, readout_status_crd, hw_serial_number);
    status->fw_version         = EXTRACT(*status_crd, readout_status_crd, fw_version);

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Inquire the communication status of the readout.
 * @param device Katherine device
 * @param status Retrieve status information
 * @return Error code.
 */
int
katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_comm_status(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_COMMUNICATION_STATUS, crd);
    if (res) goto err;

    const uint64_t *status_crd = (const uint64_t *) &crd;
    status->comm_lines_mask    = EXTRACT(*status_crd, comm_status_crd, comm_lines_mask);
    status->data_rate          = 5u * EXTRACT(*status_crd, comm_status_crd, total_data_rate);
    status->chip_detected      = EXTRACT(*status_crd, comm_status_crd, chip_detected_flag);

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Retrieve Timepix3 chip identifier.
 * @param device Katherine device
 * @param s_chip_id Start of string buffer of size `KATHERINE_CHIP_ID_STR_SIZE`
 * @return Error code.
 */
int
katherine_get_chip_id(katherine_device_t *device, char *s_chip_id)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_echo_chip_id(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_ECHO_CHIP_ID, crd);
    if (res) goto err;

    int chip_id = *(int *) crd;

    // A response whose identifier fields are all zero cannot come from a
    // readout: the chip letter is encoded one-based, so a real identifier
    // always has a nonzero low nibble. What it actually is, is this very
    // command echoed back at its sender -- a request sent to one of the
    // host's own addresses with no readout listening is delivered straight
    // back to the wildcard-bound control socket, and a command and its
    // response differ only in the fields the readout fills in. Response
    // correlation cannot help here for that very reason -- the echo carries
    // the operation code of the request, because it *is* the request -- so
    // this check remains the only thing that catches it. It is a
    // protocol-level condition, not a communication failure, so it is
    // reported as one.
    if (chip_id == 0) {
        res = -KATHERINE_E_PROTO;
        goto err;
    }

    int x = (chip_id & 0xF) - 1;
    int y = (chip_id >> 4) & 0xF;
    int w = (chip_id >> 8) & 0xFFF;

    memset(s_chip_id, '\0', KATHERINE_CHIP_ID_STR_SIZE);
    snprintf(s_chip_id, KATHERINE_CHIP_ID_STR_SIZE, "%c%d-W%04d", 65 + x, y, w);

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure the temperature of the readout.
 * @param device Katherine device
 * @param temperature Measured temperature in Celsius.
 * @return Error code.
 */
int
katherine_get_readout_temperature(katherine_device_t *device, float *temperature)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_readout_temperature(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_HW_READOUT_TEMPERATURE, crd);
    if (res) goto err;

    *temperature = *(float *) crd;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure the temperature of the sensor chip.
 * @param device Katherine device
 * @param temperature Measured temperature in Celsius.
 * @return Error code.
 */
int
katherine_get_sensor_temperature(katherine_device_t *device, float *temperature)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_sensor_temperature(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_SENSOR_TEMPERATURE, crd);
    if (res) goto err;

    *temperature = *(float *) crd;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Test communication between the readout and the sensor chip (may take several seconds).
 * @param device Katherine device
 * @return Error code.
 */
int
katherine_perform_digital_test(katherine_device_t *device)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_digital_test(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    int attempts = 100; // 10 seconds

    do {
        // This can take a while, spin for a limited amount of attempts.
        res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_DIGITAL_TEST, crd);
        --attempts;
    } while (res && attempts);

    if (res) goto err;

    if (crd[0] != 64) {
        // The test did not go well: the sensor answered, but not with the
        // expected result, and no enumerator names the specific failure.
        res = -KATHERINE_E_HW_UNKNOWN;
        goto err;
    }

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure ADC voltage.
 * @param device Katherine device
 * @param channel_id Index of the measured ADC channel
 * @param voltage Retrieved voltage
 * @return Error code.
 */
int
katherine_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_adc_voltage(&device->control_socket, channel_id);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_ADC_VOLTAGE, crd);
    if (res) goto err;

    *voltage = *(float *) crd;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}
