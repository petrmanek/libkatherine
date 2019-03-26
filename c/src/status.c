/* Katherine Control Library
 *
 * This file was created on 14.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#include <stdio.h>
#include <string.h>
#include <katherine/status.h>
#include <katherine/global.h>
#include <katherine/device.h>
#include "command_interface.h"
#include "crd.h"

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

    res = katherine_cmd_get_readout_status(&device->control_socket);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    const uint64_t *status_crd = (const uint64_t *) &crd;
    status->hw_type             = EXTRACT(*status_crd, readout_status_crd, hw_type);
    status->hw_revision         = EXTRACT(*status_crd, readout_status_crd, hw_revision);
    status->hw_serial_number    = EXTRACT(*status_crd, readout_status_crd, hw_serial_number);
    status->fw_version          = EXTRACT(*status_crd, readout_status_crd, fw_version);

    return 0;

err:
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

    res = katherine_cmd_get_comm_status(&device->control_socket);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    const uint64_t *status_crd = (const uint64_t *) &crd;
    status->comm_lines_mask         = EXTRACT(*status_crd, comm_status_crd, comm_lines_mask);
    status->data_rate               = 5u * EXTRACT(*status_crd, comm_status_crd, total_data_rate);
    status->chip_detected           = EXTRACT(*status_crd, comm_status_crd, chip_detected_flag);

    return 0;

err:
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

    res = katherine_cmd_echo_chip_id(&device->control_socket);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    int chip_id = *(int*) crd;
    int x = (chip_id & 0xF) - 1;
    int y = (chip_id >> 4) & 0xF;
    int w = (chip_id >> 8) & 0xFFF;

    memset(s_chip_id, '\0', KATHERINE_CHIP_ID_STR_SIZE);
    sprintf(s_chip_id, "%c%d-W000%d", 65 + x, y, w);
    return 0;

err:
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

    res = katherine_cmd_get_readout_temperature(&device->control_socket);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    *temperature = *(float*) crd;
    return 0;

err:
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

    res = katherine_cmd_get_sensor_temperature(&device->control_socket);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    *temperature = *(float*) crd;
    return 0;

err:
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

    res = katherine_cmd_digital_test(&device->control_socket);
    if (res) goto err;

    char crd[8];
    int attempts = 100; // 10 seconds

    do {
        // This can take a while, spin for a limited amount of attempts.
        res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
        --attempts;
    } while (res && attempts);

    if (res) goto err;

    if (crd[0] != 64) {
        // The test did not go well.
        res = 1;
        goto err;
    }

    return 0;

err:
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

    res = katherine_cmd_get_adc_voltage(&device->control_socket, channel_id);
    if (res) goto err;

    char crd[8];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, crd);
    if (res) goto err;

    *voltage = *(float*) crd;
    return 0;

err:
    return res;
}
