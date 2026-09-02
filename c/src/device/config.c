/**
 * \file
 * \brief Implementation of detector and readout configuration.
 * \author Petr Mánek
 * \date 14.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must precede every include: msleep.h needs nanosleep(), which glibc hides
// under a strict -std= unless _POSIX_C_SOURCE is set before the first libc
// header resolves feature-test macros (a once-per-TU decision). Harmless on
// Windows, whose headers do not gate on it. Same reasoning as tools/ksim.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>
#include <katherine/config.h>
#include <katherine/device.h>
#include <katherine/error.h>
#include "protocol/cmd_interface.h"
#include "msleep.h"

/**
 * Set detector configuration.
 *
 * The configuration is applied step by step in a fixed order, and the first
 * failing step returns at once -- leaving the steps that already succeeded in
 * force on the readout.
 *
 * \param device Katherine device
 * \param config Detector configuration to set
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   configuration commands within the control session's receive timeout; the
 *   pixel-matrix upload and the test-pulse command wait longer than that, see
 *   katherine_set_all_pixel_config() and katherine_set_test_pulses().
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_INVAL if the test-pulse settings are out of range --
 *   which is checked only when test pulses are enabled, see
 *   katherine_set_test_pulses() -- or if a socket call rejected its
 *   arguments.
 * \retval KATHERINE_E_IO if a command could not be sent, or a response could
 *   not be received, for a reason none of the other codes cover; see
 *   sendto(2), recvfrom(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_configure(katherine_device_t *device, const katherine_config_t *config)
{
    katherine_error_t res;

    res = katherine_set_all_pixel_config(device, &config->pixel_config);
    if (res) goto err;

    res = katherine_set_acq_time(device, config->acq_time);
    if (res) goto err;

    res = katherine_set_no_frames(device, config->no_frames);
    if (res) goto err;

    res = katherine_set_bias(device, config->bias_id, config->bias);
    if (res) goto err;

    res = katherine_acquisition_setup(device, &config->start_trigger, config->delayed_start, &config->stop_trigger);
    if (res) goto err;

    res = katherine_set_sensor_register(device, TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(config));
    if (res) goto err;

    res = katherine_set_sensor_register(device, TPX3_REG_PLL_CONFIG, katherine_pll_config_word(config));
    if (res) goto err;

    // Commits the output block's own configuration, which this library never
    // writes: the readout firmware owns the channel mask and the link clock
    // selector, and reports the mask it settled on through the communication
    // status. So this is a commit for a register nobody here sets, kept
    // because the sensor still needs the trigger to latch what the firmware
    // put there.
    res = katherine_output_block_config_update(device);
    if (res) goto err;

    // Carries the register writes above to the sensor. Without it they sit in
    // the readout's register image and never reach the chip.
    res = katherine_update_sensor_registers(device);
    if (res) goto err;

    res = katherine_timer_set(device);
    if (res) goto err;

    res = katherine_set_dacs(device, &config->dacs);
    if (res) goto err;

    // The test-pulse command carries the enable flag as well as the pulse
    // parameters, and it is the only thing that needs to: GeneralConfig's
    // Tp_en bit is not load-bearing. Forcing that bit to zero and repeating
    // this configuration on hardware fires exactly the 256 pixels of a
    // painted 16x16 patch, for analog and digital pulses alike, which is
    // also how the vendor tool and the reference implementation drive it.
    // Issued last so the register updates above cannot override it, and
    // skipped when pulses are disabled, keeping the wire traffic identical
    // to configurations that predate test pulse support.
    if (config->test_pulse_config.enabled) {
        res = katherine_set_test_pulses(device, &config->test_pulse_config);
        if (res) goto err;
    }

    return KATHERINE_E_OK;

err:
    return res;
}

static inline void
recover_from_incomplete_set_all_pixel_config(katherine_device_t *device)
{
    char *words = (char *) malloc(1024);
    if (words == NULL) return;

    // Fill buffer with bytes such that if they were understood as commands,
    // nothing bad would happen.
    for (int i = 0; i < 1024; ++i) {
        words[i] = CMD_TYPE_GET_HW_READOUT_TEMPERATURE;
    }

    // Transmit the contents of the buffer three times (assuming lossy UDP).
    for (int i = 0; i < 3 * 64; ++i) {
        // NOTE: ignoring error code below
        (void) katherine_cmd_send(&device->control_socket, words, 1024);

        if (i % 16 == 15) {
            // Same pacing as the upload itself: the flood must not outrun the readout either.
            katherine_msleep(10);
        }
    }

    // The readout's command dispatcher has no default branch and answers nothing for an opcode it does not
    // recognize, so the filler flood above provokes no responses of its own. The drain below is for whatever
    // legitimate response may still be in flight regardless: most notably the upload acknowledgement this recovery
    // was entered to replace, which the client gave up waiting for but the readout may yet deliver. Response
    // correlation (katherine_cmd_wait_ack_crd(), protocol/cmd_interface.h) steps over anything identified as some
    // other command's, but that acknowledgement carries the upload's own operation code and would therefore be read
    // as the answer to the retry below -- leaving an upload that is still incomplete looking finished, from where the
    // readout swallows the rest of the session as configuration data. Only a wait can tell the two apart, so
    // receiving continues until it times out, i.e. until the socket has been quiet for a whole receive timeout. It is
    // bounded regardless, so that a peer talking without pause cannot hold this loop forever.
    static const int max_drain = 512;
    size_t recv_size;
    int res = 0;
    for (int attempts = max_drain; attempts > 0 && !res; --attempts) {
        recv_size = 1024;
        res       = katherine_udp_recv(&device->control_socket, words, &recv_size);

        // Counted in the same tally as everything katherine_cmd_drain()
        // discards: what is thrown away here is a response belonging to no
        // request in flight, which is exactly what that counter reports.
        if (!res) ++device->control_socket.stray_command_responses;
    }

    free(words);
}

/**
 * Configure all pixels of the detector.
 *
 * The upload is attempted up to ten times, and every failed attempt is
 * followed by the recovery flood that puts the readout's command dispatcher
 * back in step -- so a call that fails leaves the pixel matrix wrong, but the
 * session usable. Only the last attempt's failure is reported.
 *
 * \param device Katherine device
 * \param px_config Configuration matrix to set
 *
 * \retval KATHERINE_E_OK on success, once the matrix has been uploaded,
 *   acknowledged and loaded into the pixel registers.
 * \retval KATHERINE_E_TIMEOUT if the readout acknowledged none of the ten
 *   uploads, each of which waits about a second for it, or if it did not
 *   acknowledge one of the two matrix commands that follow within the control
 *   session's receive timeout -- those two are sent once and not retried.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command, one of the 64 matrix chunks or one
 *   of the matrix commands could not be sent, or a response could not be
 *   received, for a reason none of the other codes cover; see sendto(2),
 *   recvfrom(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // This upload is the one exchange whose data travels between the command
    // and its acknowledgement, so it flushes and correlates by hand rather
    // than through katherine_cmd_transact(). The flush is placed here and not
    // per attempt: what a failed attempt leaves behind is cleared by the
    // recovery below, whose blocking drain is the only thing that can wait
    // out an acknowledgement still in flight -- and it has to be, because a
    // stale acknowledgement of *this* command correlates with a retry of it
    // perfectly.
    katherine_cmd_drain(&device->control_socket);

    // The following section sometimes cause issues, repeat it several times if need be.
    static const int max_attempts = 10;

    // Receives spent waiting for the acknowledgement of one attempt, each bounded by the socket receive timeout.
    static const int max_ack_attempts = 10;

    int attempts = max_attempts;
    while (attempts > 0) {
        if (attempts != max_attempts) {
            // Wait a bit between attempts.
            katherine_msleep(300);
        }

        --attempts;

        // Send command.
        res = katherine_cmd_set_all_pixel_config(&device->control_socket);
        if (res) continue;

        // Send pixel configuration data.
        const char *config = (const char *) px_config->words;
        for (int i = 0; i < 64; ++i) {
            res = katherine_cmd_send(&device->control_socket, config + 1024 * i, 1024);
            if (res) break;

            if (i % 16 == 15) {
                // Pause after every sixteenth chunk so the readout can drain what it has received so far. Pacing
                // every single chunk stretched the upload to 640 ms without helping reliability any further.
                katherine_msleep(10);
            }
        }

        if (!res) {
            // If all pixel data were transmitted, wait for acknowledgement. The readout answers once it has counted
            // all of the configuration words, and a single wait is bounded by the socket receive timeout (100 ms),
            // which a busy host or a readout still absorbing the last chunks can outlast. Since the alternative is the
            // recovery below -- some two hundred filler datagrams and a full retry -- keep receiving until the
            // acknowledgement arrives, an unrelated error occurs, or roughly a second has elapsed.
            int ack_attempts = max_ack_attempts;
            do {
                --ack_attempts;
                res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_SET_ALL_PIXEL_CONFIG);
            } while (res == KATHERINE_E_TIMEOUT && ack_attempts > 0);
        }

        if (!res) {
            // If everything went well up to this point, jump out of the loop.
            attempts = -1;
        } else {
            // At this point, something is wrong. However, if we would simply return here with error, all subsequent commands
            // would be incorrectly interpreted as pixel data and fail on ACK timeout. For that reason we will send some more
            // dummy data. This will result in incorrect pixel configuration but at least the readout will respond to any
            // commands in the future. Since we do not know how many bytes of data the readout still expects, we will
            // ridiculously overestimate it (by factor of 3). The filler is inert: with every byte set to 0x15, an
            // 8-byte prefix read as a command carries the operation code 0x1515 (bytes 6 and 7 of the repeating
            // pattern), which names no real command and is not acted upon.
            recover_from_incomplete_set_all_pixel_config(device);
        }
    }

    // Here the result is either zero (pixel data trasmitted and acknowledged) or non-zero (all attempts exhausted).
    if (res) goto err;

    // Execute HW command 5. Every hardware command is acknowledged under the
    // dispatch opcode, not under its sub-command number: the readout echoes
    // the operation code of the request it answers, and the sub-command
    // travels in the payload.
    res = katherine_cmd_hw_reset_matrix_sequential(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    // Execute HW command 9.
    res = katherine_cmd_hw_load_pixel_register_configuration(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set acquisition time of a single frame.
 *
 * The value travels in two commands, low half first, each acknowledged on its
 * own: a failure of the second leaves the readout holding a mixture of the
 * old and the new acquisition time.
 *
 * \param device Katherine device
 * \param ns Acquisition time in nanoseconds
 *
 * \retval KATHERINE_E_OK on success, once both halves have been acknowledged.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   two halves within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if either half could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_acq_time(katherine_device_t *device, double ns)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    int64_t acqt = (int64_t) (ns / 10.);
    int64_t lsb  = (acqt & 0x00000000FFFFFFFF);
    int64_t msb  = (acqt & 0xFFFFFFFF00000000) >> 32;

    katherine_cmd_drain(&device->control_socket);

    // Set LSB.
    res = katherine_cmd_set_acqtime_lsb(&device->control_socket, lsb);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB);
    if (res) goto err;

    // Set MSB.
    res = katherine_cmd_set_acqtime_msb(&device->control_socket, msb);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_ACQUISITION_TIME_SETTING_MSB);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set acquisition mode.
 * \param device Katherine device
 * \param acq_mode Acquisition mode to set
 * \param fast_vco_enabled Flag indicating the use of fast clock signal
 *
 * \retval KATHERINE_E_OK on success, once the mode has reached the sensor.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the mode
 *   command, or the sensor-register flush that carries it to the chip, within
 *   the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if one of the two commands could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_acq_mode(katherine_device_t *device, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_ACQUISITION_MODE_SETTING;

    // This command is a GeneralConfig accessor: the readout merges byte 0
    // into Op_mode [2:1] and byte 1 into Fast_lo_en [6] of its own register
    // image. Reading that image back with GET_BACK_READ_REGISTER on the
    // GeneralConfig index shows exactly that -- 0x58, 0x5a and 0x5c as the
    // mode is swept, and bit 6 clearing when byte 1 goes to zero.
    cmd[0] |= acq_mode;
    cmd[1] |= fast_vco_enabled;

    res = katherine_cmd_transact(&device->control_socket, cmd, sizeof(cmd), NULL);
    if (res) goto err;

    // The merge above only touches the readout's image; the sensor needs the
    // image flushed to it. Skipping the flush leaves the chip in whatever
    // state katherine_configure() last pushed -- Op_mode 0 with the fast
    // oscillator on -- so every acquisition silently yielded ToA+ToT data
    // regardless of the mode asked for.
    res = katherine_cmd_hw_sensor_config_registers_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set detector bias voltage.
 * \param device Katherine device
 * \param bias_id Index of the bias voltage (the value is discarded by implementation)
 * \param bias_value Bias voltage in Volts
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   bias setting.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the bias
 *   setting within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error(). The bias value itself is not
 *   validated here.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value)
{
    (void) bias_id;

    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // TODO use bias_id

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_set_bias_settings(&device->control_socket, bias_value);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_BIAS_SETTINGS);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set number of acquired frames.
 * \param device Katherine device
 * \param no_frames Number of frames
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   frame count.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the frame
 *   count within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error(). The frame count itself is
 *   not validated here.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_no_frames(katherine_device_t *device, int no_frames)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_set_number_of_frames(&device->control_socket, no_frames);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_NUMBER_OF_FRAMES);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Signal start of sequential readout.
 *
 * The readout applies this command in silence, so nothing is waited for and
 * none of the acknowledgement codes can arise: success means the datagram was
 * handed to the socket, not that the readout acted on it.
 *
 * \param device Katherine device
 * \param arg Implementation-defined argument
 *
 * \retval KATHERINE_E_OK on success, once the command has been handed to the
 *   socket.
 * \retval KATHERINE_E_IO if the command could not be sent for a reason none
 *   of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send;
 *   see sendto(2).
 * \retval KATHERINE_E_INVAL if the send or the session lock rejected its
 *   arguments; see sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if taking the control session's lock reported
 *   EAGAIN or ETIMEDOUT; see pthread_mutex_lock(3). No receive timeout is
 *   involved, since this command is not acknowledged.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be taken
 *   for any other reason; see pthread_mutex_lock(3) and
 *   katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_seq_readout_start(katherine_device_t *device, int arg)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_set_seq_readout_start(&device->control_socket, arg);
    if (res) goto err;

    // Note: this command does _not_ produce an acknowledgement.

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Prepare readout for acquisition.
 * \param device Katherine device
 * \param start_trigger I/O trigger signalling acquisition start
 * \param delayed_start Flag indicating whether acquisition start is delayed
 * \param end_trigger I/O trigger signalling acquisition end
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   trigger setup.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the trigger
 *   setup within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error(). An out-of-range trigger
 *   channel is masked rather than rejected.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_ACQUISITION_SETUP;
    cmd[4]      = 0x05;

    // This is the Gen1 trigger-word layout (manual sec. 1.2.19): a 3-bit
    // channel, edge at bit 4, delayed-start at bit 5. The Gen2 FPGA decodes
    // a different layout -- a 4-bit channel with edge and delayed-start each
    // shifted up one bit -- and the Gen2 manual's own trigger-word table is
    // wrong for its own hardware: it still describes this Gen1 layout. Do
    // not "correct" the encoding below from either a Gen2 source or that
    // table; it is right for the Gen1 wire format it targets.
    //
    // The channel occupies three bits (1..3, manual sec. 1.2.19): the mask
    // keeps an out-of-range channel from bleeding into the edge and
    // delayed-start flags above it.
    cmd[0] |= start_trigger->enabled;
    cmd[0] |= (start_trigger->channel & 0x7) << 1;
    cmd[0] |= start_trigger->use_falling_edge << 4;
    cmd[0] |= delayed_start << 5;

    cmd[1] |= end_trigger->enabled;
    cmd[1] |= (end_trigger->channel & 0x7) << 1;
    cmd[1] |= end_trigger->use_falling_edge << 4;

    res = katherine_cmd_transact(&device->control_socket, cmd, sizeof(cmd), NULL);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Configure test pulse injection.
 *
 * Test pulses are generated by the readout while the shutter is open, i.e.
 * during a subsequent acquisition. Only pixels with the test bit set in the
 * pixel configuration matrix receive pulses. In analog mode, the injected
 * amplitude is given by the difference of the VTP_coarse and VTP_fine DAC
 * output voltages.
 *
 * If test pulses are disabled by the given configuration, an explicit
 * "test pulses off" command is sent and the remaining fields are ignored.
 * Likewise, when the external pulse source is selected, the count, period
 * and phase fields do not apply and are neither validated nor transmitted.
 *
 * The readout acknowledges this command only after applying it, which takes
 * about one second; this function blocks accordingly.
 *
 * \param device Katherine device
 * \param tp_config Test pulse configuration to set
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   command, i.e. once it has applied it.
 * \retval KATHERINE_E_INVAL if the internal pulse generator was asked for a
 *   zero pulse count, a period outside 65..16321 pixel-clock cycles or a
 *   phase above 15 -- reported before anything is sent, so the readout is
 *   left untouched -- or if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the command
 *   within roughly five seconds -- fifty receives of the control session's
 *   timeout, spent waiting out the second the readout takes to apply it.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *tp_config)
{
    katherine_error_t res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_TEST_PULSE_SETTING;

    if (tp_config->enabled) {
        if (!tp_config->external) {
            if (tp_config->count == 0
                || tp_config->period < 65 || tp_config->period > 16321
                || tp_config->phase > 15) {
                return KATHERINE_E_INVAL;
            }

            cmd[0] = tp_config->count & 0xFF;
            cmd[1] = (tp_config->count >> 8) & 0xFF;
            cmd[2] = ((tp_config->period - 1) / 64) & 0xFF;
            cmd[3] = tp_config->phase;
        }

        cmd[4] |= tp_config->digital_only;
        cmd[4] |= tp_config->external << 1;
        cmd[4] |= 1 << 2;
    }

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // Sent and awaited by hand rather than through katherine_cmd_transact(),
    // whose single wait this command outlives: the retrying wait below has to
    // reissue the receive, never the command.
    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_send(&device->control_socket, cmd, sizeof(cmd));
    if (res) goto err;

    // The readout applies this command before acknowledging it, and its
    // firmware sleeps for a full second while doing so. A single wait is
    // bounded by the socket receive timeout (100 ms) and would therefore
    // always expire, so keep receiving until the acknowledgement arrives,
    // an unrelated error occurs, or roughly 5 seconds elapse.
    static const int max_attempts = 50;
    int attempts                  = max_attempts;
    do {
        --attempts;
        res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_TEST_PULSE_SETTING);
    } while (res == KATHERINE_E_TIMEOUT && attempts > 0);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set value of a sensor register.
 *
 * The write lands in a register image held by the readout, not in the sensor.
 * It reaches the chip only once katherine_update_sensor_registers() flushes
 * that image, and a caller who omits the flush gets no error and no effect --
 * the sensor simply goes on using whatever a previous flush left there.
 * Several registers may be written before one flush commits them together.
 *
 * \param device Katherine device
 * \param reg_idx Index of the register
 * \param reg_value Value to assign to the register
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   write into its register image.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the write
 *   within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error(). Neither the register index
 *   nor the value is validated here.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 *
 * \see katherine_update_sensor_registers
 */
katherine_error_t
katherine_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_send64_i64(
        &device->control_socket, CMD_TYPE_SENSOR_REGISTER_SETTING, (uint8_t) reg_idx, reg_value);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_SENSOR_REGISTER_SETTING);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Carry the readout's sensor register image to the sensor.
 *
 * This is the commit half of every sensor register write: values set by
 * katherine_set_sensor_register(), and the pixel mode and fast-oscillator
 * flag set by katherine_set_acq_mode(), all sit in the readout's image until
 * this flushes them. Registers written and never flushed have no effect
 * whatsoever, silently, so treat the pairing as mandatory.
 *
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   flush.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the flush
 *   within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 *
 * \see katherine_set_sensor_register
 */
katherine_error_t
katherine_update_sensor_registers(katherine_device_t *device)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_hw_sensor_config_registers_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Update output block config.
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   hardware command.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the hardware
 *   command within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_output_block_config_update(katherine_device_t *device)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_hw_output_block_config_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set timer.
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   hardware command.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the hardware
 *   command within the control session's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_timer_set(katherine_device_t *device)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    katherine_cmd_drain(&device->control_socket);

    res = katherine_cmd_hw_timer_set(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set DAC register values.
 *
 * Values are transmitted unchecked: a value wider than its DAC's bit width
 * (Tpx3 manual Table 11) is truncated by the chip, not rejected here. Call
 * katherine_dacs_validate() first if that matters to the caller; this
 * function's own behavior is unchanged from the 1.x series.
 *
 * The eighteen registers are written one acknowledged command at a time and
 * applied to the sensor by a nineteenth, so a call that fails part-way leaves
 * the readout holding a mixture of old and new values which the sensor has
 * not been given.
 *
 * \param device Katherine device
 * \param dacs DAC register values to set
 *
 * \retval KATHERINE_E_OK on success, once all eighteen writes and the update
 *   command have been acknowledged.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   writes, or the update command, within the control session's receive
 *   timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if one of the commands could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_INVAL if a socket call rejected its arguments; see
 *   sendto(2) and katherine_udp_last_os_error(). Out-of-range DAC values are
 *   not reported here, as described above.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs)
{
    katherine_error_t res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // Nineteen exchanges under one lock, flushed once: each of them
    // correlates its own response, so a flush before every send would only
    // repeat work the correlation already does.
    katherine_cmd_drain(&device->control_socket);

    for (int i = 0; i < 18; ++i) {
        res = katherine_cmd_send64_i64(
            &device->control_socket, CMD_TYPE_INTERNAL_DAC_SETTINGS, (uint8_t) i, dacs->array[i]);
        if (res) goto err;

        res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_INTERNAL_DAC_SETTINGS);
        if (res) goto err;
    }

    res = katherine_cmd_hw_internal_dac_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return KATHERINE_E_OK;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

// Per-DAC maxima (Tpx3 manual Table 11, "DAC Value" column width), in
// katherine_dacs_named_t / array order, i.e. chip DAC Code minus one.
static const uint16_t KATHERINE_DAC_MAX[18] = {
    255, // Ibias_Preamp_ON     [7:0]
    15,  // Ibias_Preamp_OFF    [3:0]
    255, // VPReamp_NCAS        [7:0]
    255, // Ibias_Ikrum         [7:0]
    255, // Vfbk                [7:0]
    511, // Vthreshold_fine     [8:0]
    15,  // Vthreshold_coarse   [3:0]
    255, // Ibias_DiscS1_ON     [7:0]
    15,  // Ibias_DiscS1_OFF    [3:0]
    255, // Ibias_DiscS2_ON     [7:0]
    15,  // Ibias_DiscS2_OFF    [3:0]
    255, // Ibias_PixelDAC      [7:0]
    255, // Ibias_TPbufferIn    [7:0]
    255, // Ibias_TPbufferOut   [7:0]
    255, // VTP_coarse          [7:0]
    511, // VTP_fine            [8:0]
    255, // Ibias_CP_PLL        [7:0]
    255, // PLL_Vcntrl          [7:0]
};

/**
 * Pixel-clock phase counts, Tpx3 manual Table 17 (p39), for DualEdgeClock = 1
 * -- the value katherine_pll_config_word() pins. Rows are the clock divider
 * in katherine_freq_t order, columns the requested phase count in
 * katherine_phase_t order.
 *
 * The manual's other half, DualEdgeClock = 0, is one column further clamped
 * throughout: {1,2,4,8,16}, {1,1,2,4,8}, {1,1,1,2,4}, {1,1,1,1,2}. It is not
 * tabulated here because nothing in this library clears that bit; were it
 * ever exposed, this table gains a dimension rather than changing shape.
 */
static const uint8_t KATHERINE_ACTUAL_PHASES[4][5] = {
    // PHASE_1 PHASE_2 PHASE_4 PHASE_8 PHASE_16
    /* FREQ_20  */ {1, 2, 4, 8, 16},
    /* FREQ_40  */ {1, 2, 4, 8, 16},
    /* FREQ_80  */ {1, 1, 2, 4, 8},
    /* FREQ_160 */ {1, 1, 1, 2, 4},
};

/**
 * Number of pixel-clock phases a frequency and phase setting actually yield.
 *
 * The phase enumerators name what they request, not what they get: the clock
 * divider clamps the count, so PHASE_16 yields 16 phases at FREQ_40 but only
 * 4 at FREQ_160 (Tpx3 manual Table 17). Anything dividing a coarse tick by
 * the number of phases -- per-double-column timestamp correction above all --
 * has to divide by this, not by the enumerator's name.
 *
 * \param freq Pixel-clock frequency selector.
 * \param phase Requested phase count.
 * \return Phases actually generated, or 0 if either argument is out of range.
 */
uint8_t
katherine_actual_phases(katherine_freq_t freq, katherine_phase_t phase)
{
    // Signed comparison on purpose: both parameters are enumerations, and a
    // caller passing a negative value would otherwise index far outside the
    // table after conversion to an unsigned index.
    if ((int) freq < 0 || (int) freq > FREQ_160) return 0;
    if ((int) phase < 0 || (int) phase > PHASE_16) return 0;

    return KATHERINE_ACTUAL_PHASES[freq][phase];
}

/**
 * Whether fast time stamping is coherent at a given pixel-clock frequency.
 *
 * Fine time stamping subtracts a 4-bit counter running at 640 MHz from the
 * coarse ToA, so the documented Timestamp = ToA - fToA holds only where those
 * 16 fine ticks are exactly one ToA tick. The condition is bounded on both
 * sides, which is easy to miss: where the ToA tick is shorter, the fine field
 * reaches past the tick it belongs to; where it is longer, the field cannot
 * close the gap. Tpx3 manual Table 17 answers the same question in its own
 * terms, under "Fast Time Stamping".
 *
 * Ask this function rather than testing a frequency against one chosen in
 * advance. Which settings satisfy the condition follows from the sensor and
 * the readout rather than from this API, so a caller that hard-codes today's
 * answer inherits a silent breakage the day a generation answers differently.
 *
 * Both failure directions were observed with test pulses into one pixel per
 * column: where the fine field matches the tick, the per-double-column clock
 * stagger resolves cleanly; where it does not, the measurement is noise.
 *
 * \see katherine_acquisition_begin
 *
 * \param freq Pixel-clock frequency selector.
 * \return true if fast time stamping is coherent at this frequency.
 */
bool
katherine_freq_is_fast_vco_supported(katherine_freq_t freq)
{
    // Table 17 answers "Fast Time Stamping" Yes for the Fin/8 divider alone,
    // in both DualEdgeClock rows.
    return freq == FREQ_40;
}

/**
 * Validate DAC register values against the chip's per-DAC bit widths
 * (Tpx3 manual Table 11: each of the 18 DACs is 4, 8 or 9 bits wide).
 *
 * This check is opt-in: katherine_set_dacs() transmits every value unchecked,
 * and a value wider than its DAC's field is silently truncated by the chip
 * rather than rejected there. Calling this function first is a caller's
 * choice; it does not change katherine_set_dacs()'s own behavior.
 *
 * \param v DAC register values to validate.
 * \return 0 if every value fits its DAC's range, KATHERINE_E_INVAL otherwise.
 */
katherine_error_t
katherine_dacs_validate(const katherine_dacs_t *v)
{
    for (int i = 0; i < 18; ++i) {
        if (v->array[i] > KATHERINE_DAC_MAX[i]) return KATHERINE_E_INVAL;
    }

    return KATHERINE_E_OK;
}
