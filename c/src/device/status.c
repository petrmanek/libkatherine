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
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the status
 *   request within the control session's receive timeout, or if datagrams from
 *   other hosts kept arriving until the pinned session's discard budget ran
 *   out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   status request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the status request or receiving its reply
 *   failed at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the status request or receiving its
 *   reply ran out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the
 *   communication-status request within the control session's receive
 *   timeout, or if datagrams from other hosts kept arriving until the pinned
 *   session's discard budget ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the
 *   chip-identifier request within the control session's receive timeout, or
 *   if datagrams from other hosts kept arriving until the pinned session's
 *   discard budget ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_PROTO if the answer's identifier word was zero, which
 *   no readout sends -- the chip letter is encoded one-based, so a real
 *   identifier has a nonzero low nibble. In practice this is the request
 *   itself delivered back to the sender, which response correlation cannot
 *   reject because the echo carries the request's own operation code; see the
 *   comment at the check.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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

    int chip_id;
    memcpy(&chip_id, crd, sizeof(chip_id));

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
 * Reads a sensor on the readout board and never reaches the chip, so
 * unlike katherine_get_sensor_temperature() this remains available while an
 * acquisition is running and is the one to poll during a measurement.
 *
 * \param device Katherine device
 * \param temperature Measured temperature in Celsius.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the temperature
 *   request within the control session's receive timeout, or if datagrams
 *   from other hosts kept arriving until the pinned session's discard budget
 *   ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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

    memcpy(temperature, crd, sizeof(*temperature));

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Measure the chip's temperature, as its own on-die sensor reports it.
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
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if an acquisition is currently started on this
 *   device. This is a precondition, not a communication failure: it is
 *   checked before the session lock is taken and before anything is sent, so
 *   the readout is left untouched. The caller must not have an acquisition in
 *   flight -- none started yet, or the last one already read to its end or
 *   aborted -- and should poll katherine_get_readout_temperature() instead
 *   while one is running.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the temperature
 *   request within the control session's receive timeout, or if datagrams
 *   from other hosts kept arriving until the pinned session's discard budget
 *   ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_get_sensor_temperature(katherine_device_t *device, float *temperature)
{
    katherine_error_t res;

    if (device->acquisition != NULL) {
        res = KATHERINE_E_STATE;
        goto err_busy;
    }

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_get_sensor_temperature(&device->control_socket);
    if (res) goto err;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_SENSOR_TEMPERATURE, crd);
    if (res) goto err;

    memcpy(temperature, crd, sizeof(*temperature));

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
err_busy:
    return res;
}

/**
 * Test communication between the readout and the chip (may take several seconds).
 *
 * The wait for the result is retried up to a hundred times, because the test
 * itself outlasts the control session's receive timeout by far. A code raised
 * in the wait is therefore the hundredth attempt's and arrives some ten
 * seconds in, not at the first timeout.
 *
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success, meaning the test ran and passed.
 * \retval KATHERINE_E_HW_UNKNOWN if the test ran but did not pass. The sensor
 *   answered, and byte 0 of its answer carries the result: only the value 64,
 *   every one of the 64 matrix patterns returned correct, counts as a pass.
 *   Any other value is a fault on the readout-to-sensor link that the
 *   protocol does not break down further, so nothing here says which pattern
 *   failed or how.
 * \retval KATHERINE_E_TIMEOUT if none of the hundred attempts saw an answer
 *   within the control session's receive timeout, or if datagrams from other
 *   hosts kept arriving until the pinned session's discard budget ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer the voltage
 *   request within the control session's receive timeout, or if datagrams
 *   from other hosts kept arriving until the pinned session's discard budget
 *   ran out.
 * \retval KATHERINE_E_BAD_CRD if the answer was not exactly the
 *   KATHERINE_CMD_CRD_SIZE bytes the protocol fixes a command response at.
 * \retval KATHERINE_E_STRAY if responses identified as some other command's
 *   kept arriving until the discard budget ran out before this one's did.
 * \retval KATHERINE_E_INVAL if taking the control session's lock, sending the
 *   request, or receiving its reply reported an invalid argument -- never for
 *   an out-of-range \p channel_id, which is not validated here; see
 *   pthread_mutex_lock(3), sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if sending the request or receiving its reply failed
 *   at the OS level for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if sending the request or receiving its reply ran
 *   out of memory; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
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

    memcpy(voltage, crd, sizeof(*voltage));

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Read a bias supply's voltage back.
 *
 * The readout converts for us, so this is volts. Quantised at about 0.505 V
 * per step of its 12-bit converter, and slightly negative at rest.
 *
 * \param device Katherine device
 * \param bias_id Index of the bias supply
 * \param voltage Retrieved voltage, in Volts
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   control session's receive timeout, or strays exhausted the discard budget.
 * \retval KATHERINE_E_BAD_CRD if the answer was not a command response.
 * \retval KATHERINE_E_STRAY if another command's responses kept arriving
 *   until the discard budget ran out.
 * \retval KATHERINE_E_INVAL if \p bias_id is not one this readout provides,
 *   or if a socket call or the lock rejected an argument; see sendto(2),
 *   recvfrom(2), pthread_mutex_lock(3) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so how many bias supplies it has is unknown.
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3).
 */
katherine_error_t
katherine_get_bias(katherine_device_t *device, uint8_t bias_id, float *voltage)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    if (bias_id >= device->derived_info.bias_supply_count) {
        res = KATHERINE_E_INVAL;
        goto err_bias_id;
    }

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err_lock;

    katherine_cmd_drain(&device->control_socket);

    // Both bytes carry the index: the manual specifies byte 4, the Gen2
    // firmware reads byte 0, and the reference implementation sets both.
    res = katherine_cmd_send64_i64(&device->control_socket, CMD_TYPE_GET_BIAS_VOLTAGE, bias_id, bias_id);
    if (res) goto err_send;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_BIAS_VOLTAGE, crd);
    if (res) goto err_recv;

    if (voltage != NULL) memcpy(voltage, crd, sizeof(*voltage));

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
err_lock:
err_bias_id:
err_enumerated:
    return res;
}

/**
 * Read the leakage current flowing through the sensor.
 *
 * The zero point is not calibrated per unit, so readings carry an offset of
 * up to about 0.4 uA.
 *
 * \param device Katherine device
 * \param bias_id Index of the bias supply
 * \param current Retrieved leakage current, in microamperes
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_UNSUPPORTED if the device is not known to answer this
 *   command: one first-generation firmware spends the same operation code on a
 *   communication-setup command reading the same byte, so it is refused rather
 *   than sent hopefully.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its generation is unknown.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   control session's receive timeout, which is also what a measurement in
 *   flight produces: the operation code is absent from the Gen2 firmware's
 *   mid-acquisition dispatcher.
 * \retval KATHERINE_E_BAD_CRD if the answer was not a command response.
 * \retval KATHERINE_E_STRAY if another command's responses kept arriving
 *   until the discard budget ran out.
 * \retval KATHERINE_E_INVAL if \p bias_id is not one this readout provides,
 *   or if a socket call or the lock rejected an argument; see sendto(2),
 *   recvfrom(2) and pthread_mutex_lock(3).
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3).
 */
katherine_error_t
katherine_get_bias_leakage(katherine_device_t *device, uint8_t bias_id, float *current)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    if (device->derived_info.gen < 2) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_generation;
    }

    if (bias_id >= device->derived_info.bias_supply_count) {
        res = KATHERINE_E_INVAL;
        goto err_bias_id;
    }

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err_lock;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_send64_i64(&device->control_socket, CMD_TYPE_GET_BIAS_CURRENT, bias_id, bias_id);
    if (res) goto err_send;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_GET_BIAS_CURRENT, crd);
    if (res) goto err_recv;

    // Both from the reference implementation alone: no datasheet, manual or
    // firmware source states either, and both are second generation only.
    //
    // TODO: measure the zero per unit. It is not a constant -- one readout
    //   here reads 2.0264 V at no current -- so it belongs in the device,
    //   with a calibration entry point.
    // TODO: measure the scale properly, with a bias scan against a known
    //   load, rather than inheriting 166.67 uA/V from one line of the
    //   reference.
    // TODO: find the first-generation equivalent. The reference carries the
    //   same open question, and this call refuses below the second generation
    //   partly because of it.
    static const float gen2_sense_zero_v = 2.024f;
    static const float gen2_sense_ohms   = 6000.0f;

    float sense;
    memcpy(&sense, crd, sizeof(sense));
    if (current != NULL) *current = 1e6f * (gen2_sense_zero_v - sense) / gen2_sense_ohms;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
err_lock:
err_bias_id:
err_generation:
err_enumerated:
    return res;
}

/**
 * Read the voltage at one DAC monitor, in Volts.
 *
 * katherine_tpx3_dac_to_si() gives the SI value the DAC's setpoint was meant
 * to produce, which is what this measurement is to be compared against.
 *
 * The chip multiplexes one analog output, so a monitor has to settle before
 * it can be read; the call blocks for a few milliseconds.
 *
 * \param device Katherine device
 * \param chip_index Index of the chip to read
 * \param monitor Monitor to select
 * \param voltage Retrieved voltage, in Volts
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so how many chips it can address is unknown.
 * \retval KATHERINE_E_BAD_CHIP if the chip index is past what this readout
 *   can address; see katherine_device_derived_info_t::max_chip_count.
 * \retval KATHERINE_E_INVAL if the monitor is outside the enumeration, or if
 *   a socket call or the lock rejected an argument; see sendto(2),
 *   recvfrom(2) and pthread_mutex_lock(3).
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if the answer was not a command response.
 * \retval KATHERINE_E_STRAY if another command's responses kept arriving
 *   until the discard budget ran out.
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3).
 * \see katherine_tpx3_dac_to_si
 */
katherine_error_t
katherine_tpx3_get_dac_monitor_voltage(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_dac_monitor_t monitor, float *voltage)
{
    // The chip's own five-bit index of what its one analog output carries,
    // Tpx3 manual Table 11. Zero is the chip's SenseOFF, which carries
    // nothing, and so stands in for a monitor outside the enumeration.
    static const uint8_t MONITOR_INDEX[KATHERINE_TPX3_DAC_MONITOR_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 28, 29, 30, 31};

    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_unsupported;
    }

    if (chip_index >= device->derived_info.max_chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip_index;
    }

    const uint8_t monitor_index = (unsigned) monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT ? MONITOR_INDEX[monitor] : 0;
    if (monitor_index == 0) {
        res = KATHERINE_E_INVAL;
        goto err_monitor;
    }

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err_lock;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_send601(&device->control_socket, CMD_TYPE_INTERNAL_DAC_SCAN, monitor_index, chip_index);
    if (res) goto err_send;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_socket, CMD_TYPE_INTERNAL_DAC_SCAN, crd);
    if (res) goto err_recv;

    if (voltage != NULL) memcpy(voltage, crd, sizeof(*voltage));

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
err_lock:
err_monitor:
err_chip_index:
err_unsupported:
    return res;
}
