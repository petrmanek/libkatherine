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
 * Every per-chip step addresses chip 0. What a readout carrying more than one
 * chip should be given -- this configuration repeated, or one configuration
 * per chip -- is a question about katherine_config_t, which holds one of
 * everything.
 *
 * \param device Katherine device
 * \param config Detector configuration to set
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   configuration commands within the control link's receive timeout; the
 *   pixel-matrix upload and the test-pulse command wait longer than that, see
 *   katherine_tpx3_set_all_pixel_config() and
 *   katherine_tpx3_set_test_pulses().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_INVAL if the test-pulse settings are out of range --
 *   which is checked only when test pulses are enabled, see
 *   katherine_tpx3_set_test_pulses() -- or if the control link rejected an
 *   argument of the exchange.
 * \retval KATHERINE_E_IO if a command could not be sent, or a response could
 *   not be received, for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_UNSUPPORTED if the end trigger's delay is armed on a
 *   readout of the first generation; see katherine_set_acquisition_triggers().
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, or is one this version cannot drive: nothing below can be
 *   addressed to a chip, and the acquisition time's encoding depends on the
 *   readout generation. See katherine_set_acq_time().
 * \retval KATHERINE_E_BAD_CHIP if the device reports no chips at all, chip 0
 *   included; see katherine_device_info_t::chip_count.
 */
katherine_error_t
katherine_configure(katherine_device_t *device, const katherine_config_t *config)
{
    katherine_error_t res;

    // Checked here rather than left to the first step that needs it, so that
    // a device nobody has enumerated is told so instead of being told it
    // carries no chip 0 -- which is true, and useless.
    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err;
    }

    res = katherine_tpx3_set_all_pixel_config(device, 0, &config->pixel_config);
    if (res) goto err;

    res = katherine_set_acq_time(device, config->acq_time);
    if (res) goto err;

    res = katherine_set_no_frames(device, config->no_frames);
    if (res) goto err;

    res = katherine_set_bias(device, config->bias_index, config->bias);
    if (res) goto err;

    res = katherine_set_acquisition_triggers(device, &config->start_trigger, &config->stop_trigger);
    if (res) goto err;

    res = katherine_tpx3_set_sensor_register(
        device, 0, KATHERINE_TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(config));
    if (res) goto err;

    res = katherine_tpx3_set_sensor_register(device, 0, KATHERINE_TPX3_REG_PLL_CONFIG, katherine_pll_config_word(config));
    if (res) goto err;

    // Commits the output block's own configuration, which this library never
    // writes: the readout firmware owns the channel mask and the link clock
    // selector, and reports the mask it settled on through the communication
    // status. So this is a commit for a register nobody here sets, kept
    // because the sensor still needs the trigger to latch what the firmware
    // put there.
    res = katherine_tpx3_output_block_config_update(device, 0);
    if (res) goto err;

    // Carries the register writes above to the sensor. Without it they sit in
    // the readout's register image and never reach the chip.
    res = katherine_tpx3_update_sensor_registers(device, 0);
    if (res) goto err;

    res = katherine_tpx3_timer_set(device, 0);
    if (res) goto err;

    res = katherine_tpx3_set_dacs(device, 0, &config->dacs);
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
        res = katherine_tpx3_set_test_pulses(device, &config->test_pulse_config);
        if (res) goto err;
    }

    return KATHERINE_E_OK;

err:
    return res;
}

/**
 * Unwedge a readout left mid-upload by a failed pixel-configuration send.
 *
 * Feeds it the filler bytes the firmware is still waiting for, then drains
 * the acknowledgement that follows. Best-effort by nature: there is nothing
 * useful to report to a caller already handling the original failure.
 *
 * \param device Device whose upload was interrupted
 */
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
        (void) katherine_cmd_send(&device->control_transport, words, 1024);

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
        res       = katherine_transport_recv_crd(&device->control_transport, words, &recv_size);

        // Counted in the same tally as everything the transport's own flush
        // discards: what is thrown away here is a response belonging to no
        // request in flight, which is exactly what that counter reports.
        if (!res) ++device->control_transport.stray_command_responses;
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
 * \param chip_index Chip to address, counting from zero
 * \param px_config Configuration matrix to set
 *
 * \retval KATHERINE_E_OK on success, once the matrix has been uploaded,
 *   acknowledged and loaded into the pixel registers.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout acknowledged none of the ten
 *   uploads, each of which waits about a second for it, or if it did not
 *   acknowledge one of the two matrix commands that follow within the control
 *   link's receive timeout -- those two are sent once and not retried.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command, one of the 64 matrix chunks or one of
 *   the matrix commands could not be sent, or a response could not be
 *   received, for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_all_pixel_config(katherine_device_t *device, uint8_t chip_index, const katherine_tpx3_px_config_t *px_config)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    // This upload is the one exchange whose data travels between the command
    // and its acknowledgement, so it flushes and correlates by hand rather
    // than through katherine_cmd_transact(). The flush is placed here and not
    // per attempt: what a failed attempt leaves behind is cleared by the
    // recovery below, whose blocking drain is the only thing that can wait
    // out an acknowledgement still in flight -- and it has to be, because a
    // stale acknowledgement of *this* command correlates with a retry of it
    // perfectly.
    katherine_transport_flush_incoming(&device->control_transport);

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
        res = katherine_cmd_send6_chip(
            &device->control_transport, CMD_TYPE_SET_ALL_PIXEL_CONFIG, chip_index);
        if (res) continue;

        // Send pixel configuration data.
        const char *config = (const char *) px_config->words;
        for (int i = 0; i < 64; ++i) {
            res = katherine_cmd_send(&device->control_transport, config + 1024 * i, 1024);
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
                res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_SET_ALL_PIXEL_CONFIG);
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
    if (res) goto err_upload;

    // Execute HW command 5. Every hardware command is acknowledged under the
    // dispatch opcode, not under its sub-command number: the readout echoes
    // the operation code of the request it answers, and the sub-command
    // travels in the payload.
    res = katherine_cmd_send60_chip(&device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_RESET_MATRIX_SEQUENTIAL, chip_index);
    if (res) goto err_reset_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_reset_recv;

    // Execute HW command 9.
    res = katherine_cmd_send60_chip(&device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_LOAD_PIXEL_REGISTER_CONFIGURATION, chip_index);
    if (res) goto err_load_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_load_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_load_recv:
err_load_send:
err_reset_recv:
err_reset_send:
err_upload:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Set a Gen1 acquisition time, as a count of 10 ns ticks in two halves.
 *
 * The value travels in two commands, low half first, each acknowledged on its
 * own: a failure of the second leaves the readout holding a mixture of the old
 * and the new acquisition time.
 *
 * \param device Katherine device
 * \param ns Acquisition time in nanoseconds
 *
 * \retval KATHERINE_E_OK on success, once both halves have been acknowledged.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   two halves within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if either half could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
static katherine_error_t
katherine_set_acq_time_gen1(katherine_device_t *device, double ns)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    int64_t acqt = (int64_t) (ns / 10.);
    int64_t lsb  = (acqt & 0x00000000FFFFFFFF);
    int64_t msb  = (acqt & 0xFFFFFFFF00000000) >> 32;

    // Set LSB.
    res = katherine_cmd_set_acqtime_lsb(&device->control_transport, lsb);
    if (res) goto err_lsb_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB);
    if (res) goto err_lsb_recv;

    // Set MSB.
    res = katherine_cmd_set_acqtime_msb(&device->control_transport, msb);
    if (res) goto err_msb_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_ACQUISITION_TIME_SETTING_MSB);
    if (res) goto err_msb_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_msb_recv:
err_msb_send:
err_lsb_recv:
err_lsb_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Set a Gen2 acquisition time, as IEEE-754 seconds in one command.
 *
 * Gen2 carries the whole value in the payload of 0x01 and has no 0x0A at all:
 * the opcode is absent from its dispatcher, and the firmware does not
 * acknowledge an opcode it does not know. Sending the Gen1 pair here would
 * therefore not merely write a wrong number, it would hang on an
 * acknowledgement that can never arrive.
 *
 * \param device Katherine device
 * \param ns Acquisition time in nanoseconds
 *
 * \retval KATHERINE_E_OK on success, once the command has been acknowledged.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the command
 *   within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
static katherine_error_t
katherine_set_acq_time_gen2(katherine_device_t *device, double ns)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_set_acqtime_seconds(&device->control_transport, (float) (ns / 1e9));
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Set acquisition time of a single frame.
 *
 * The two generations encode this differently and incompatibly, so this
 * dispatches on what the readout turned out to be and the two functions above
 * hold one wire sequence each. Each of them takes and releases the session
 * lock itself, so this adds nothing but the choice.
 *
 * The choice needs katherine_device_info_t to have been populated, which
 * katherine_device_init() does by probing. A readout that never answered that
 * probe reports generation 0 and is treated as Gen1, since that is what every
 * readout this library drove before Gen2 was recognized.
 *
 * \param device Katherine device
 * \param ns Acquisition time in nanoseconds
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge within the
 *   control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if a command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, or is one this version cannot drive: the encoding depends on the
 *   readout generation, which is then not known.
 */
katherine_error_t
katherine_set_acq_time(katherine_device_t *device, double ns)
{
    katherine_error_t res;

    // Generation-dependent, so it needs to know the generation. A device that
    // has been neither enumerated nor declared has a zeroed derived record,
    // whose supported flag is false -- and so does one whose hardware type
    // this version does not recognize, or recognizes and cannot drive. All
    // three are refused here rather than silently taking the Gen1 path, which
    // on a Gen2 readout would hang on an acknowledgement that never comes.
    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_unsupported;
    }

    if (device->derived_info.gen >= 2) {
        res = katherine_set_acq_time_gen2(device, ns);
    } else {
        res = katherine_set_acq_time_gen1(device, ns);
    }

    if (res) {
        goto err_set_acq_time;
    }

    return KATHERINE_E_OK;

err_set_acq_time:
err_unsupported:
    return res;
}

/**
 * Set the readout's Timepix3 token count.
 *
 * An eight-bit value in the payload of 0x29, and a Timepix3 setting rather
 * than a per-generation one: the reference implementation sends it in every
 * Timepix3 configuration and skips it for Timepix2 and Timepix4. Its default
 * there is 17 and its documented minimum 1.
 *
 * libkatherine has never sent this command, so every acquisition to date --
 * on both generations -- has run on whatever the readout powers up with.
 * katherine_configure() still does not send it, deliberately: what a good
 * value is depends on the data rate, and the one sweep taken so far was
 * measured against a self-triggering matrix and is not evidence of anything.
 * Until that is redone at a real rate this stays a setting a caller may
 * choose, not one the library picks.
 *
 * \param device Katherine device
 * \param token_count Token count to set
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the command
 *   within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or a response could
 *   not be received, for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange.
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_token_count(katherine_device_t *device, uint8_t token_count)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_set_token_count(&device->control_transport, token_count);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_NUMBER_OF_TOKENS_SETTING);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Set acquisition mode.
 * \param device Katherine device
 * \param px_mode Acquisition mode to set
 * \param fast_vco_enabled Flag indicating the use of fast clock signal
 *
 * \retval KATHERINE_E_OK on success, once the mode has reached the sensor.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the mode
 *   command, or the sensor-register flush that carries it to the chip, within
 *   the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if one of the two commands could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_acq_mode(katherine_device_t *device, katherine_tpx3_px_mode_t px_mode, bool fast_vco_enabled)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_ACQUISITION_MODE_SETTING;

    // This command is a GeneralConfig accessor: the readout merges byte 0
    // into Op_mode [2:1] and byte 1 into Fast_lo_en [6] of its own register
    // image. Reading that image back with GET_BACK_READ_REGISTER on the
    // GeneralConfig index shows exactly that -- 0x58, 0x5a and 0x5c as the
    // mode is swept, and bit 6 clearing when byte 1 goes to zero.
    cmd[0] |= px_mode;
    cmd[1] |= fast_vco_enabled;

    res = katherine_cmd_transact(&device->control_transport, cmd, sizeof(cmd), NULL);
    if (res) goto err_registers;

    // The merge above only touches the readout's image; the sensor needs the
    // image flushed to it. Skipping the flush leaves the chip in whatever
    // state katherine_configure() last pushed -- Op_mode 0 with the fast
    // oscillator on -- so every acquisition silently yielded ToA+ToT data
    // regardless of the mode asked for.
    res = katherine_cmd_hw_sensor_config_registers_update(&device->control_transport);
    if (res) goto err_update_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_update_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_update_recv:
err_update_send:
err_registers:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Set detector bias voltage. The acknowledgement is of the command and not of
 * the voltage: a raised bias takes seconds to arrive at its setpoint, while
 * dropping it is prompt.
 *
 * \param device Katherine device
 * \param bias_index Supply to drive, addressed from 0.
 * \param bias_value Bias voltage in Volts. Its sign selects the supply's
 *   polarity, which the readout latches per supply.
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   bias setting. The supply ramps afterwards, so the voltage has not
 *   arrived when this returns; katherine_get_bias() is what reports it.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, since which supplies it has is then not known.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the bias
 *   setting within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if \p bias_index is not one this readout provides,
 *   or if the control link rejected an argument of the exchange; see
 *   katherine_transport_get_last_os_error(). The bias value itself is not
 *   validated here.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_set_bias(katherine_device_t *device, uint8_t bias_index, float bias_value)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    if (bias_index >= device->derived_info.bias_supply_count) {
        res = KATHERINE_E_INVAL;
        goto err_bias_id;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_set_bias_settings(&device->control_transport, bias_index, bias_value);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_BIAS_SETTINGS);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_bias_id:
err_enumerated:
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
 *   count within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error(). The frame count itself
 *   is not validated here.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_set_no_frames(katherine_device_t *device, int no_frames)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_set_number_of_frames(&device->control_transport, no_frames);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_NUMBER_OF_FRAMES);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Signal start of sequential readout.
 *
 * The readout applies this command in silence, so nothing is waited for and
 * none of the acknowledgement codes can arise: success means the command was
 * handed to the link, not that the readout acted on it.
 *
 * \param device Katherine device
 * \param arg Implementation-defined argument
 *
 * \retval KATHERINE_E_OK on success, once the command has been handed to the
 *   socket.
 * \retval KATHERINE_E_IO if the command could not be sent for a reason none of
 *   the other codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the send or the session lock rejected its
 *   arguments; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if taking the control link's lock reported
 *   EAGAIN or ETIMEDOUT; see katherine_transport_get_last_os_error(). No receive
 *   timeout is involved, since this command is not acknowledged.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken for
 *   any other reason; see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_seq_readout_start(katherine_device_t *device, int arg)
{
    katherine_error_t res;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_set_seq_readout_start(&device->control_transport, arg);
    if (res) goto err_send;

    // Note: this command does _not_ produce an acknowledgement.

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
    return res;
}

/**
 * Set the start and end triggers of an acquisition, and their delays.
 *
 * A trigger whose delay is not armed is applied at once, and its
 * katherine_trigger_t::delay_seconds is disregarded rather than refused, so
 * that a caller may arm and disarm the delay without clearing the interval.
 *
 * The first generation can delay the start of an acquisition but not its
 * end, its trigger word having no flag for the second; a delayed end trigger
 * is refused there.
 *
 * \param device Katherine device
 * \param start_trigger I/O trigger signalling acquisition start
 * \param end_trigger I/O trigger signalling acquisition end
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   trigger setup.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the trigger
 *   setup within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if an armed delay is negative, not a number or
 *   longer than this readout can hold, or the control link rejected an
 *   argument of the exchange; see katherine_transport_get_last_os_error(). An
 *   out-of-range trigger channel is masked rather than rejected.
 * \retval KATHERINE_E_UNSUPPORTED if the end trigger's delay is armed on a
 *   readout of the first generation.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its generation is unknown.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_set_acquisition_triggers(katherine_device_t *device, const katherine_trigger_t *start_trigger, const katherine_trigger_t *end_trigger)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    // Two layouts, one per generation, and the Gen2 manual's own
    // trigger-word table describes the Gen1 one -- so do not "correct" either
    // encoding from it. The first generation reads a 3-bit channel at bit 1,
    // the edge at 4 and the delayed start at 5 (manual sec. 1.2.19); the
    // second reads a 4-bit channel at bit 1, the edge at 5 and the delay at
    // 6. The masks keep an out-of-range channel from bleeding into the flags
    // above it.
    //
    // The two agree only for a rising edge on channels 0 to 7 with no delay.
    // A falling edge encoded the first way sets the top bit of the second
    // way's channel, which is how a Gen2 readout came to listen on the wrong
    // input.
    const bool gen2         = device->derived_info.gen >= 2;
    const int channel_mask  = gen2 ? 0xF : 0x7;
    const int edge_shift    = gen2 ? 5 : 4;
    const int delayed_shift = gen2 ? 6 : 5;

    // Both bytes of the second generation's word carry the delay flag, and
    // the shutter generator tests each of them; the first generation's end
    // byte has no such bit, so there is nowhere to put the request.
    if (end_trigger->delayed && !gen2) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_generation;
    }

    // What the delay registers count in, which is the readout's business and
    // not the caller's: seconds go in, and the width of the register is what
    // bounds them. A delay too long to hold is refused rather than truncated,
    // and a comparison that rejects a NaN is written the way round that does.
    static const double tick_ns = 10.0;

    const double start_f = start_trigger->delay_seconds * 1e9 / tick_ns;
    const double stop_f  = end_trigger->delay_seconds * 1e9 / tick_ns;

    if (start_trigger->delayed && !(start_f >= 0.0 && start_f <= (double) UINT32_MAX)) {
        res = KATHERINE_E_INVAL;
        goto err_start_delay;
    }

    if (end_trigger->delayed && !(stop_f >= 0.0 && stop_f <= (double) UINT32_MAX)) {
        res = KATHERINE_E_INVAL;
        goto err_stop_delay;
    }

    uint32_t start = (uint32_t) start_trigger->enabled;
    start |= (uint32_t) (start_trigger->channel & channel_mask) << 1;
    start |= (uint32_t) (start_trigger->edge == KATHERINE_TRIGGER_EDGE_FALLING) << edge_shift;
    start |= (uint32_t) start_trigger->delayed << delayed_shift;

    uint32_t end = (uint32_t) end_trigger->enabled;
    end |= (uint32_t) (end_trigger->channel & channel_mask) << 1;
    end |= (uint32_t) (end_trigger->edge == KATHERINE_TRIGGER_EDGE_FALLING) << edge_shift;
    end |= (uint32_t) end_trigger->delayed << delayed_shift;

    // One register of the acquisition unit, the start trigger in its low byte
    // and the end trigger in the next.
    const uint32_t word = (end << 8) | start;

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    // A delay register is written only for a trigger that arms it, so a
    // configuration that asks for none puts exactly the one word on the wire
    // that it always did.
    if (start_trigger->delayed) {
        res = katherine_cmd_acq_unit_write(
            &device->control_transport, KATHERINE_ACQ_UNIT_REG_SHUTTER_START_DELAY, (uint32_t) (start_f + 0.5));
        if (res) goto err_start_write;
    }

    if (end_trigger->delayed) {
        res = katherine_cmd_acq_unit_write(
            &device->control_transport, KATHERINE_ACQ_UNIT_REG_SHUTTER_FINISH_DELAY, (uint32_t) (stop_f + 0.5));
        if (res) goto err_stop_write;
    }

    // Last, because it is what arms the delays above.
    res = katherine_cmd_acq_unit_write(&device->control_transport, KATHERINE_ACQ_UNIT_REG_TRIGGER_SETUP, word);
    if (res) goto err_write;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_write:
err_stop_write:
err_start_write:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_stop_delay:
err_start_delay:
err_generation:
err_enumerated:
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
 *   zero pulse count, a period outside 65..16321 pixel-clock cycles or a phase
 *   above 15 -- reported before anything is sent, so the readout is left
 *   untouched -- or if the control link rejected an argument of the exchange;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the command
 *   within roughly five seconds -- fifty receives of the control link's
 *   timeout, spent waiting out the second the readout takes to apply it.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_test_pulses(katherine_device_t *device, const katherine_tpx3_test_pulse_config_t *tp_config)
{
    katherine_error_t res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_TEST_PULSE_SETTING;

    if (tp_config->enabled) {
        if (!tp_config->external) {
            if (tp_config->count == 0
                || tp_config->period < 65 || tp_config->period > 16321
                || tp_config->phase > 15) {
                res = KATHERINE_E_INVAL;
                goto err_check;
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

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    // Sent and awaited by hand rather than through katherine_cmd_transact(),
    // whose single wait this command outlives: the retrying wait below has to
    // reissue the receive, never the command.
    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send(&device->control_transport, cmd, sizeof(cmd));
    if (res) goto err_send;

    // The readout applies this command before acknowledging it, and its
    // firmware sleeps for a full second while doing so. A single wait is
    // bounded by the socket receive timeout (100 ms) and would therefore
    // always expire, so keep receiving until the acknowledgement arrives,
    // an unrelated error occurs, or roughly 5 seconds elapse.
    static const int max_attempts = 50;
    int attempts                  = max_attempts;
    do {
        --attempts;
        res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_TEST_PULSE_SETTING);
    } while (res == KATHERINE_E_TIMEOUT && attempts > 0);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_check:
    return res;
}

/**
 * Set value of a sensor register.
 *
 * The write lands in a register image held by the readout, not in the sensor.
 * It reaches the chip only once katherine_tpx3_update_sensor_registers() flushes
 * that image, and a caller who omits the flush gets no error and no effect --
 * the sensor simply goes on using whatever a previous flush left there.
 * Several registers may be written before one flush commits them together.
 *
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 * \param reg_idx Index of the register
 * \param reg_value Value to assign to the register
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   write into its register image.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the write
 *   within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error(). Neither the register
 *   index nor the value is validated here.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 *
 * \see katherine_tpx3_update_sensor_registers
 */
katherine_error_t
katherine_tpx3_set_sensor_register(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_reg_t reg_idx, int32_t reg_value)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send64_i64_chip(
        &device->control_transport, CMD_TYPE_SENSOR_REGISTER_SETTING, (uint8_t) reg_idx, reg_value, chip_index);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_SENSOR_REGISTER_SETTING);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Carry the readout's sensor register image to the sensor.
 *
 * This is the commit half of every sensor register write: values set by
 * katherine_tpx3_set_sensor_register(), and the pixel mode and fast-oscillator
 * flag set by katherine_tpx3_set_acq_mode(), all sit in the readout's image until
 * this flushes them. Registers written and never flushed have no effect
 * whatsoever, silently, so treat the pairing as mandatory.
 *
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   flush.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the flush
 *   within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 *
 * \see katherine_tpx3_set_sensor_register
 */
katherine_error_t
katherine_tpx3_update_sensor_registers(katherine_device_t *device, uint8_t chip_index)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send60_chip(
        &device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE, chip_index);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Update output block config.
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   hardware command.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the hardware
 *   command within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_output_block_config_update(katherine_device_t *device, uint8_t chip_index)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send60_chip(
        &device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE, chip_index);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Set timer.
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 *
 * \retval KATHERINE_E_OK on success, once the readout has acknowledged the
 *   hardware command.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the hardware
 *   command within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if the command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the send or
 *   the receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_timer_set(katherine_device_t *device, uint8_t chip_index)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send60_chip(
        &device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_TIMER_SET, chip_index);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Set DAC register values.
 *
 * Values are transmitted unchecked: a value wider than its DAC's bit width
 * (Tpx3 manual Table 11) is truncated by the chip, not rejected here. Call
 * katherine_tpx3_dacs_validate() first if that matters to the caller; this
 * function's own behavior is unchanged from the 1.x series.
 *
 * The eighteen registers are written one acknowledged command at a time and
 * applied to the sensor by a nineteenth, so a call that fails part-way leaves
 * the readout holding a mixture of old and new values which the sensor has
 * not been given.
 *
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 * \param dacs DAC register values to set
 *
 * \retval KATHERINE_E_OK on success, once all eighteen writes and the update
 *   command have been acknowledged.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   writes, or the update command, within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response arrived whose length is not that
 *   of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_IO if one of the commands could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of the
 *   exchange; see katherine_transport_get_last_os_error(). Out-of-range DAC values
 *   are not reported here, as described above.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_dacs(katherine_device_t *device, uint8_t chip_index, const katherine_tpx3_dacs_t *dacs)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    // Nineteen exchanges under one lock, flushed once: each of them
    // correlates its own response, so a flush before every send would only
    // repeat work the correlation already does.
    katherine_transport_flush_incoming(&device->control_transport);

    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        res = katherine_cmd_send64_i64_chip(&device->control_transport,
            CMD_TYPE_INTERNAL_DAC_SETTINGS, (uint8_t) i, dacs->array[i], chip_index);
        if (res) goto err_dac_send;

        res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_INTERNAL_DAC_SETTINGS);
        if (res) goto err_dac_recv;
    }

    res = katherine_cmd_send60_chip(&device->control_transport, CMD_TYPE_HW_COMMAND_START,
        CMD_START_INTERNAL_DAC_UPDATE, chip_index);
    if (res) goto err_update_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_update_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_update_recv:
err_update_send:
err_dac_recv:
err_dac_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
    return res;
}

/**
 * Set one DAC on the chip, and have the chip take it.
 *
 * The same exchange katherine_tpx3_set_dacs() makes for each of the eighteen,
 * for a caller changing one: a write into the readout's shadow of the chip's
 * registers, then the update command that loads all of them into the chip and
 * latches them. The other seventeen keep whatever was last written, so this
 * is not a way to set a DAC on a chip that was never configured -- the shadow
 * starts at whatever the readout was holding.
 *
 * The chip has no path for reading a DAC setpoint back, so a caller tracking
 * what it has set must remember it. katherine_tpx3_read_dac_monitor() reports
 * the voltage at the multiplexer, which is the analogue result rather than
 * the number that produced it.
 *
 * \param device Katherine device
 * \param chip_index Chip to address, counting from zero
 * \param dac DAC to set
 * \param value Register value to write, in the same units as the
 *   corresponding katherine_tpx3_dacs_t member
 *
 * \retval KATHERINE_E_OK on success, once both the write and the update
 *   command have been acknowledged.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold. A device that has been neither enumerated nor
 *   declared carries none, so this refuses chip 0 as well.
 * \retval KATHERINE_E_INVAL if \a dac is not one of the chip's DACs. A value
 *   too large for the DAC is not reported: like katherine_tpx3_set_dacs(),
 *   this transmits it unchecked, and katherine_tpx3_dacs_validate() is the
 *   opt-in check.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the write or
 *   the update command within the control link's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response datagram arrived whose length is
 *   not that of a command response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while the
 *   session kept delivering responses belonging to other commands.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if a command could not be sent, or its
 *   acknowledgement could not be received, for a reason none of the other
 *   codes cover; see sendto(2), recvfrom(2) and
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for a send or a
 *   receive; see sendto(2) and recvfrom(2).
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and
 *   katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_set_dac(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_dac_t dac, uint16_t value)
{
    katherine_error_t res;

    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    if ((int) dac < 0 || (int) dac >= KATHERINE_TPX3_DAC_COUNT) {
        res = KATHERINE_E_INVAL;
        goto err_dac;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send64_i64_chip(
        &device->control_transport, CMD_TYPE_INTERNAL_DAC_SETTINGS, (uint8_t) dac, value, chip_index);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_INTERNAL_DAC_SETTINGS);
    if (res) goto err_ack;

    res = katherine_cmd_send60_chip(
        &device->control_transport, CMD_TYPE_HW_COMMAND_START, CMD_START_INTERNAL_DAC_UPDATE, chip_index);
    if (res) goto err_update;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_HW_COMMAND_START);
    if (res) goto err_update_ack;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_update_ack:
err_update:
err_ack:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_dac:
err_chip:
    return res;
}

/**
 * Per-DAC metadata, in katherine_tpx3_dacs_named_t / array order, i.e. chip DAC
 * Code minus one.
 *
 * `max` is the "DAC Value" column width of Tpx3 manual Table 11. `lsb` and
 * `unit` are Table 28's step and quantity, in SI units.
 *
 * The conversion below scales by `lsb` rather than by the range Table 28
 * also states, because the two disagree: lsb x max exceeds the stated range
 * for Ibias_Ikrum (+2.0%), Vthreshold_coarse (+0.8%), Ibias_PixelDAC (+0.9%)
 * and PLL_Vcntrl (+7.7%), and falls short for Ibias_DiscS2_ON and
 * Ibias_DiscS2_OFF (-2.5%). The manual corroborates the step where it can:
 * its combined 13-bit Vthreshold row gives 0 to 1.45 V, and
 * 15 x 80 mV + 511 x 500 uV is 1.4555 V, so for the coarse threshold it is
 * the stated range that is rounded, not the step. test_dacs_metadata.c pins
 * every one of those discrepancies, so a transcription slip cannot hide in
 * them.
 */
typedef struct {
    const char *name;          ///< The DAC's name, as the Tpx3 manual spells it.
    uint16_t max;              ///< Largest accepted value; inclusive.
    double lsb;                ///< SI units per step.
    katherine_dac_unit_t unit; ///< Quantity lsb is measured in.
} katherine_dac_info_t;

/** The table itself, one row per DAC. */
static const katherine_dac_info_t KATHERINE_TPX3_DAC_INFO[KATHERINE_TPX3_DAC_COUNT] = {
    // clang-format off
  // name                  max  lsb       unit
    {"Ibias_Preamp_ON",   255, 20e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_Preamp_OFF",  15,  20e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Vpreamp_NCAS",      255, 5e-3,    KATHERINE_DAC_UNIT_VOLT},
    {"Ibias_Ikrum",       255, 240e-12, KATHERINE_DAC_UNIT_AMP},
    {"Vfbk",              255, 5e-3,    KATHERINE_DAC_UNIT_VOLT},
    {"Vthreshold_fine",   511, 500e-6,  KATHERINE_DAC_UNIT_VOLT},
    {"Vthreshold_coarse", 15,  80e-3,   KATHERINE_DAC_UNIT_VOLT},
    {"Ibias_DiscS1_ON",   255, 20e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_DiscS1_OFF",  15,  20e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_DiscS2_ON",   255, 13e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_DiscS2_OFF",  15,  13e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_PixelDAC",    255, 1.08e-9, KATHERINE_DAC_UNIT_AMP},
    {"Ibias_TPbufferIn",  255, 40e-9,   KATHERINE_DAC_UNIT_AMP},
    {"Ibias_TPbufferOut", 255, 1e-6,    KATHERINE_DAC_UNIT_AMP},
    {"VTP_coarse",        255, 5e-3,    KATHERINE_DAC_UNIT_VOLT},
    {"VTP_fine",          511, 2.5e-3,  KATHERINE_DAC_UNIT_VOLT},
    {"Ibias_CP_PLL",      255, 600e-9,  KATHERINE_DAC_UNIT_AMP},
    {"PLL_Vcntrl",        255, 5.7e-3,  KATHERINE_DAC_UNIT_VOLT},
    // clang-format on
};
/**
 * Pixel-clock phase counts, Tpx3 manual Table 17 (p39), for DualEdgeClock = 1
 * -- the value katherine_pll_config_word() pins. Rows are the clock divider
 * in katherine_tpx3_freq_t order, columns the requested phase count in
 * katherine_tpx3_phase_t order.
 *
 * The manual's other half, DualEdgeClock = 0, is one column further clamped
 * throughout: {1,2,4,8,16}, {1,1,2,4,8}, {1,1,1,2,4}, {1,1,1,1,2}. It is not
 * tabulated here because nothing in this library clears that bit; were it
 * ever exposed, this table gains a dimension rather than changing shape.
 */
static const uint8_t KATHERINE_ACTUAL_PHASES[4][5] = {
    // clang-format off
    // Both axes are labelled by number alone. The doc block above names the
    // enumerations the rows and columns follow, and spelling either prefix out
    // nine times over would stretch the table past the point of reading as one.
    //
    // MHz \ phases  1  2  4  8  16
    /*  20 */      {  1, 2, 4, 8, 16 },
    /*  40 */      {  1, 2, 4, 8, 16 },
    /*  80 */      {  1, 1, 2, 4,  8 },
    /* 160 */      {  1, 1, 1, 2,  4 },
    // clang-format on
};

/**
 * Number of pixel-clock phases a frequency and phase setting actually yield.
 *
 * The phase enumerators name what they request, not what they get: the clock
 * divider clamps the count, so KATHERINE_TPX3_PHASE_16 yields 16 phases at
 * KATHERINE_TPX3_FREQ_40_MHZ but only 4 at KATHERINE_TPX3_FREQ_160_MHZ (Tpx3
 * manual Table 17). Anything dividing a coarse tick by the number of phases
 * -- per-double-column timestamp correction above all -- has to divide by
 * this, not by the enumerator's name.
 *
 * \param freq Pixel-clock frequency selector.
 * \param phase Requested phase count.
 * \return Phases actually generated, or 0 if either argument is out of range.
 */
uint8_t
katherine_tpx3_actual_phases(katherine_tpx3_freq_t freq, katherine_tpx3_phase_t phase)
{
    // Signed comparison on purpose: both parameters are enumerations, and a
    // caller passing a negative value would otherwise index far outside the
    // table after conversion to an unsigned index.
    if ((int) freq < 0 || (int) freq > KATHERINE_TPX3_FREQ_160_MHZ) return 0;
    if ((int) phase < 0 || (int) phase > KATHERINE_TPX3_PHASE_16) return 0;

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
katherine_tpx3_freq_is_fast_vco_supported(katherine_tpx3_freq_t freq)
{
    // Table 17 answers "Fast Time Stamping" Yes for the Fin/8 divider alone,
    // in both DualEdgeClock rows.
    return freq == KATHERINE_TPX3_FREQ_40_MHZ;
}

/**
 * Validate DAC register values against the chip's per-DAC bit widths
 * (Tpx3 manual Table 11: each of the 18 DACs is 4, 8 or 9 bits wide).
 *
 * This check is opt-in: katherine_tpx3_set_dacs() transmits every value unchecked,
 * and a value wider than its DAC's field is silently truncated by the chip
 * rather than rejected there. Calling this function first is a caller's
 * choice; it does not change katherine_tpx3_set_dacs()'s own behavior.
 *
 * \param v DAC register values to validate.
 *
 * \retval KATHERINE_E_OK if every value fits its DAC's range.
 * \retval KATHERINE_E_INVAL if any value is wider than its DAC's field, the
 *   array being in katherine_tpx3_dacs_named_t order. Which one is not
 *   reported: the caller holds the values and the widths are in the manual.
 */
katherine_error_t
katherine_tpx3_dacs_validate(const katherine_tpx3_dacs_t *v)
{
    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        if (v->array[i] > KATHERINE_TPX3_DAC_INFO[i].max) return KATHERINE_E_INVAL;
    }

    return KATHERINE_E_OK;
}

/**
 * Name of a Timepix3 DAC, as its manual spells it.
 * \param dac DAC to name
 * \return Null-terminated string, owned by the library. "unknown" for a value
 *   outside the enumeration.
 */
const char *
katherine_tpx3_dac_name(katherine_tpx3_dac_t dac)
{
    if ((unsigned) dac >= KATHERINE_TPX3_DAC_COUNT) return "unknown";

    return KATHERINE_TPX3_DAC_INFO[dac].name;
}

/**
 * Largest value a Timepix3 DAC accepts.
 * \param dac DAC to ask about
 * \return The maximum, inclusive: a setting equal to it is valid and
 *   katherine_tpx3_dacs_validate() accepts it, one above it is rejected. 0 for
 *   a value outside the enumeration.
 */
uint16_t
katherine_tpx3_dac_max(katherine_tpx3_dac_t dac)
{
    if ((unsigned) dac >= KATHERINE_TPX3_DAC_COUNT) return 0;

    return KATHERINE_TPX3_DAC_INFO[dac].max;
}

/**
 * Convert a Timepix3 DAC setting to SI units.
 *
 * Nominal, not calibrated: these are the design figures of Tpx3 manual
 * Table 28, and a real chip departs from them, which is why per-chip
 * calibration files exist. Good for labelling an axis or sizing a scan; not
 * a measurement.
 *
 * \param dac DAC the value belongs to
 * \param value Setting, as written to katherine_tpx3_dacs_t
 * \param unit Filled with the quantity returned, unless NULL
 * \return The nominal amperes or volts, or 0.0 for a DAC outside the
 *   enumeration. A value above the DAC's maximum is converted anyway, the
 *   scale being linear; katherine_tpx3_dacs_validate() is what rejects those.
 */
double
katherine_tpx3_dac_to_si(katherine_tpx3_dac_t dac, uint16_t value, katherine_dac_unit_t *unit)
{
    if ((unsigned) dac >= KATHERINE_TPX3_DAC_COUNT) return 0.0;

    if (unit != NULL) *unit = KATHERINE_TPX3_DAC_INFO[dac].unit;

    return KATHERINE_TPX3_DAC_INFO[dac].lsb * (double) value;
}

/** Names of the monitoring outputs, Tpx3 manual Table 11. */
static const char *const KATHERINE_TPX3_DAC_MONITOR_INFO[] = {
    "BandGap output",
    "BandGap_Temp",
    "Ibias_dac",
    "Ibias_dac_cas",
};

/**
 * Name of a DAC monitor.
 * \param monitor Monitor to name
 * \return The DAC's name for the first eighteen monitors, the monitoring
 *   output's name for the four after them, "unknown" outside the enumeration.
 */
const char *
katherine_tpx3_dac_monitor_name(katherine_tpx3_dac_monitor_t monitor)
{
    if ((unsigned) monitor < KATHERINE_TPX3_DAC_COUNT) {
        return katherine_tpx3_dac_name((katherine_tpx3_dac_t) monitor);
    }
    if ((unsigned) monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT) {
        return KATHERINE_TPX3_DAC_MONITOR_INFO[(unsigned) monitor - KATHERINE_TPX3_DAC_COUNT];
    }

    return "unknown";
}


/**
 * Ask the readout whether the chip is still holding the configuration it was
 * given.
 *
 * Two flags, both the chip's own answer rather than a comparison this library
 * makes: whether the pixel matrix read back without a parity error, and
 * whether every DAC read back as written.
 *
 * A flag is only as current as the answer it came from. Anything that writes
 * the configuration after this call invalidates both, and a call that fails
 * leaves them untouched.
 *
 * \see katherine_configure(), katherine_tpx3_set_all_pixel_config(),
 *   katherine_tpx3_set_dacs()
 *
 * \param device Katherine device
 * \param chip_index Index of the chip to ask about
 * \param check Where the two flags are written, or NULL to discard them
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_UNSUPPORTED if the readout is of the first generation,
 *   whose firmware has no such operation.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so how many chips it can address is unknown.
 * \retval KATHERINE_E_BAD_CHIP if the readout carries no such chip; see
 *   katherine_device_info_t::chip_count, which is what a readout reports and
 *   not what it could hold.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the control
 *   link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if the answer was not a command response.
 * \retval KATHERINE_E_STRAY if another command's responses kept arriving until
 *   the discard budget ran out.
 * \retval KATHERINE_E_INVAL if the control link rejected an argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_tpx3_verify_config(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_config_check_t *check)
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
    if (chip_index >= device->info.chip_count) {
        res = KATHERINE_E_BAD_CHIP;
        goto err_chip;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send60(&device->control_transport, CMD_TYPE_VERIFY_CHIP_CONFIG, chip_index);
    if (res) goto err_send;

    char crd[KATHERINE_CMD_CRD_SIZE];
    res = katherine_cmd_wait_ack_crd(&device->control_transport, CMD_TYPE_VERIFY_CHIP_CONFIG, crd);
    if (res) goto err_recv;

    if (check != NULL) {
        check->matrix_ok = crd[0] != 0;
        check->dacs_ok   = crd[1] != 0;
    }

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_chip:
err_generation:
err_enumerated:
    return res;
}
