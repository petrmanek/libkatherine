/**
 * \file
 * \brief Implementation of readout status inquiry.
 * \author Petr Mánek
 * \date 14.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
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
 * \param device Katherine device
 * \param status Retrieved status information
 * \return Error code.
 */
katherine_error_t
katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // Every inquiry below opens the same way: the session is flushed before
    // the command goes out, so a response an earlier exchange left behind
    // cannot be read as this one's. Correlation catches a response
    // identified as some other command's; only the flush catches a stale
    // response of the *same* command, which would correlate perfectly.
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
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Inquire the communication status of the readout.
 * \param device Katherine device
 * \param status Retrieve status information
 * \return Error code.
 */
katherine_error_t
katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status)
{
    katherine_error_t res;

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
    // The register counts megabytes per second, so eight bits per byte give
    // the megabits the field is documented in. The readout manual says to
    // scale by five instead, which cannot be right: a Gen1 readout reporting
    // 160 here has an output-block register of 0x0981, i.e. two active links
    // at 320 MHz dual-edge, and two links of 640 Mb/s are 1280 Mb/s, not 800.
    // Nor can 1280 be reached by fives from any 8-bit value at all.
    status->data_rate  = 8u * EXTRACT(*status_crd, comm_status_crd, total_data_rate);
    status->chip_count = EXTRACT(*status_crd, comm_status_crd, chip_count);

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Retrieve Timepix3 chip identifier.
 * \param device Katherine device
 * \param s_chip_id Start of string buffer of size `KATHERINE_CHIP_ID_STR_SIZE`
 * \return Error code.
 */
katherine_error_t
katherine_get_chip_id(katherine_device_t *device, char *s_chip_id)
{
    katherine_error_t res;

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
        res = KATHERINE_E_PROTO;
        goto err;
    }

    int x = (chip_id & 0xF) - 1;
    int y = (chip_id >> 4) & 0xF;
    int w = (chip_id >> 8) & 0xFFF;

    memset(s_chip_id, '\0', KATHERINE_CHIP_ID_STR_SIZE);
    snprintf(s_chip_id, KATHERINE_CHIP_ID_STR_SIZE, "%c%d-W%04d", 65 + x, y, w);

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure the temperature of the readout.
 *
 * Reads a sensor on the readout board and never reaches the sensor chip, so
 * unlike katherine_get_sensor_temperature() this remains available while an
 * acquisition is running and is the one to poll during a measurement.
 *
 * \param device Katherine device
 * \param temperature Measured temperature in Celsius.
 * \return Error code.
 */
katherine_error_t
katherine_get_readout_temperature(katherine_device_t *device, float *temperature)
{
    katherine_error_t res;

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
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure the temperature of the sensor chip.
 *
 * Refused with KATHERINE_E_STATE while an acquisition is running, because the
 * readout measures this by way of the DAC scan: it reloads all of the sensor
 * registers from its own image, points the sense-DAC selector at the two
 * temperature DACs, and flushes the lot to the sensor. Mid-acquisition that
 * pushes whatever registers the caller has written since the last flush --
 * the pixel mode and the fast-oscillator flag among them -- so the sensor can
 * change pixel format in the middle of the stream, and the selector is left
 * pointing elsewhere. Poll katherine_get_readout_temperature() instead, which
 * reads a readout-side sensor and never touches the chip.
 *
 * \param device Katherine device
 * \param temperature Measured temperature in Celsius.
 * \return Error code.
 */
katherine_error_t
katherine_get_sensor_temperature(katherine_device_t *device, float *temperature)
{
    katherine_error_t res;

    // Before the lock and before any I/O: nothing about this call is safe to
    // begin while a measurement is in flight.
    if (device->acquisition != NULL) return KATHERINE_E_STATE;

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
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Test communication between the readout and the sensor chip (may take several seconds).
 * \param device Katherine device
 * \return Error code.
 */
katherine_error_t
katherine_perform_digital_test(katherine_device_t *device)
{
    katherine_error_t res;

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
        res = KATHERINE_E_HW_UNKNOWN;
        goto err;
    }

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure ADC voltage.
 * \param device Katherine device
 * \param channel_id Index of the measured ADC channel
 * \param voltage Retrieved voltage
 * \return Error code.
 */
katherine_error_t
katherine_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage)
{
    katherine_error_t res;

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
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}
