/**
 * @file
 * @brief Implementation of detector and readout configuration.
 * @author Petr Mánek
 * @date 14.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
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
 * @param device Katherine device
 * @param config Detector configuration to set
 * @return Error code.
 */
int
katherine_configure(katherine_device_t *device, const katherine_config_t *config)
{
    int res;

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

    // PLL configuration word (Tpx3 manual Table 16, sensor register 3). The
    // 0xE sets four fixed one-bit fields at [3:0]: ByPassPLL = 0 (bit 0, PLL
    // on), ResetPLL = 1 (bit 1, running), SelectVcntrl_PLL_DAC = 1 (bit 2,
    // Vcntrl sourced from the PLL), DualEdgeClock = 1 (bit 3). Bits [5:4]
    // are Clk_phaseShift_divider, the clock frequency selector of Table 17;
    // bits [8:6] are Clk_phaseShift_number, the phase selector clamped by
    // that same table (see katherine_phase_t, config.h). 0x14 << 9 sets
    // PLLOutConfig[13:9] = ShutterOut, routing the shutter signal to the
    // PLL output pad.
    int32_t pll_setup = 0xE;
    pll_setup |= (0x7 & config->phase) << 6;
    pll_setup |= (0x3 & config->freq) << 4;
    pll_setup |= 0x14 << 9;
    res = katherine_set_sensor_register(device, TPX3_REG_PLL_CONFIG, pll_setup);
    if (res) goto err;

    res = katherine_output_block_config_update(device);
    if (res) goto err;

    res = katherine_update_sensor_registers(device);
    if (res) goto err;

    res = katherine_timer_set(device);
    if (res) goto err;

    res = katherine_set_dacs(device, &config->dacs);
    if (res) goto err;

    /* Note: the test pulse enable flag travels twice. The TP_en bit of the
       general configuration register above follows the sensor register
       update, actively switching test pulses on or off through documented
       commands. The remaining pulse parameters are then communicated by the
       dedicated command below; it is issued last so that the register
       updates above cannot override its effects, and skipped when test
       pulses are disabled, keeping the wire traffic identical to
       configurations that predate test pulse support. */
    if (config->test_pulse_config.enabled) {
        res = katherine_set_test_pulses(device, &config->test_pulse_config);
        if (res) goto err;
    }

    return 0;

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
    // was entered to replace, which the client gave up waiting for but the readout may yet deliver, or any other
    // response still stale on the wire. A response left queued here is read as the acknowledgement of some later
    // command, and from then on every command of the session pairs with the response of an earlier one, so nothing
    // can fail visibly again. Receiving therefore continues until it times out, i.e. until the socket has been quiet
    // for a whole receive timeout, and is bounded regardless so that a peer talking without pause cannot hold this
    // loop forever.
    static const int max_drain = 512;
    size_t recv_size;
    int res = 0;
    for (int attempts = max_drain; attempts > 0 && !res; --attempts) {
        recv_size = 1024;
        res       = katherine_udp_recv(&device->control_socket, words, &recv_size);
    }

    free(words);
}

/**
 * Configure all pixels of the detector.
 * @param device Katherine device
 * @param px_config Configuration matrix to set
 * @return Error code.
 */
int
katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

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
                res = katherine_cmd_wait_ack(&device->control_socket);
            } while (res == -KATHERINE_E_TIMEOUT && ack_attempts > 0);
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

    // Execute HW command 5.
    res = katherine_cmd_hw_reset_matrix_sequential(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    // Execute HW command 9.
    res = katherine_cmd_hw_load_pixel_register_configuration(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set acquisition time of a single frame.
 * @param device Katherine device
 * @param ns Acquisition time in nanoseconds
 * @return Error code.
 */
int
katherine_set_acq_time(katherine_device_t *device, double ns)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    int64_t acqt = (int64_t) (ns / 10.);
    int64_t lsb  = (acqt & 0x00000000FFFFFFFF);
    int64_t msb  = (acqt & 0xFFFFFFFF00000000) >> 32;

    // Set LSB.
    res = katherine_cmd_set_acqtime_lsb(&device->control_socket, lsb);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    // Set MSB.
    res = katherine_cmd_set_acqtime_msb(&device->control_socket, msb);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set acquisition mode.
 * @param device Katherine device
 * @param acq_mode Acquisition mode to set
 * @param fast_vco_enabled Flag indicating the use of fast clock signal
 * @return Error code.
 */
int
katherine_set_acq_mode(katherine_device_t *device, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_ACQUISITION_MODE_SETTING;

    cmd[0] |= acq_mode;

    // The fast-VCO flag travels in byte 0 bit 7 here. Katherine manuals
    // v0.008+ and the Gen2 readout firmware instead read it from CD[8] --
    // byte 1 bit 0 -- so this is off by one byte against the documented
    // wire format. Currently moot: the readout firmware only shadows this
    // command into a register image it never flushes to the sensor, so
    // neither placement reaches the chip either way. The correct-bit fix
    // rides with the 2.0 GeneralConfig rework, which folds this flag into
    // the sensor register write instead and removes the dependency on this
    // command.
    cmd[0] |= fast_vco_enabled << 7;

    res = katherine_cmd_send(&device->control_socket, &cmd, sizeof(cmd));
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set detector bias voltage.
 * @param device Katherine device
 * @param bias_id Index of the bias voltage (the value is discarded by implementation)
 * @param bias_value Bias voltage in Volts
 * @return Error code.
 */
int
katherine_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value)
{
    (void) bias_id;

    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    // TODO use bias_id

    res = katherine_cmd_set_bias_settings(&device->control_socket, bias_value);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set number of acquired frames.
 * @param device Katherine device
 * @param no_frames Number of frames
 * @return Error code.
 */
int
katherine_set_no_frames(katherine_device_t *device, int no_frames)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_set_number_of_frames(&device->control_socket, no_frames);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Signal start of sequential readout.
 * @param device Katherine device
 * @param arg Implementation-defined argument
 * @return Error code.
 */
int
katherine_set_seq_readout_start(katherine_device_t *device, int arg)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_set_seq_readout_start(&device->control_socket, arg);
    if (res) goto err;

    /* Note: this command does _not_ produce an acknowledgement. */

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Prepare readout for acquisition.
 * @param device Katherine device
 * @param start_trigger I/O trigger signalling acquisition start
 * @param delayed_start Flag indicating whether acquisition start is delayed
 * @param end_trigger I/O trigger signalling acquisition end
 * @return Error code.
 */
int
katherine_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger)
{
    int res;

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

    res = katherine_cmd_send(&device->control_socket, &cmd, sizeof(cmd));
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

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
 * @param device Katherine device
 * @param tp_config Test pulse configuration to set
 * @return Error code.
 */
int
katherine_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *tp_config)
{
    int res;

    char cmd[8] = {0};
    cmd[6]      = CMD_TYPE_TEST_PULSE_SETTING;

    if (tp_config->enabled) {
        if (!tp_config->external) {
            if (tp_config->count == 0
                || tp_config->period < 65 || tp_config->period > 16321
                || tp_config->phase > 15) {
                return -KATHERINE_E_INVAL;
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

    res = katherine_cmd_send(&device->control_socket, &cmd, sizeof(cmd));
    if (res) goto err;

    /* The readout applies this command before acknowledging it, and its
       firmware sleeps for a full second while doing so. A single wait is
       bounded by the socket receive timeout (100 ms) and would therefore
       always expire, so keep receiving until the acknowledgement arrives,
       an unrelated error occurs, or roughly 5 seconds elapse. */
    static const int max_attempts = 50;
    int attempts                  = max_attempts;
    do {
        --attempts;
        res = katherine_cmd_wait_ack(&device->control_socket);
    } while (res == -KATHERINE_E_TIMEOUT && attempts > 0);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set value of a sensor register.
 * @param device Katherine device
 * @param reg_idx Index of the register
 * @param reg_value Value to assign to the register
 * @return Error code.
 */
int
katherine_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_send64_i64(
        &device->control_socket, CMD_TYPE_SENSOR_REGISTER_SETTING, (uint8_t) reg_idx, reg_value);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Update sensor registers.
 * @param device Katherine device
 * @return Error code.
 */
int
katherine_update_sensor_registers(katherine_device_t *device)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_hw_sensor_config_registers_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Update output block config.
 * @param device Katherine device
 * @return Error code.
 */
int
katherine_output_block_config_update(katherine_device_t *device)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_hw_output_block_config_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/**
 * Set timer.
 * @param device Katherine device
 * @return Error code.
 */
int
katherine_timer_set(katherine_device_t *device)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    res = katherine_cmd_hw_timer_set(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

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
 * @param device Katherine device
 * @param dacs DAC register values to set
 * @return Error code.
 */
int
katherine_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) return res;

    for (int i = 0; i < 18; ++i) {
        res = katherine_cmd_send64_i64(
            &device->control_socket, CMD_TYPE_INTERNAL_DAC_SETTINGS, (uint8_t) i, dacs->array[i]);
        if (res) goto err;

        res = katherine_cmd_wait_ack(&device->control_socket);
        if (res) goto err;
    }

    res = katherine_cmd_hw_internal_dac_update(&device->control_socket);
    if (res) goto err;

    res = katherine_cmd_wait_ack(&device->control_socket);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&device->control_socket);
    return res;
}

/* Per-DAC maxima (Tpx3 manual Table 11, "DAC Value" column width), in
   katherine_dacs_named_t / array order, i.e. chip DAC Code minus one. */
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
 * Validate DAC register values against the chip's per-DAC bit widths.
 * @param v DAC register values to validate.
 * @return 0 if every value fits its DAC's range, -KATHERINE_E_INVAL otherwise.
 */
int
katherine_dacs_validate(const katherine_dacs_t *v)
{
    for (int i = 0; i < 18; ++i) {
        if (v->array[i] > KATHERINE_DAC_MAX[i]) return -KATHERINE_E_INVAL;
    }

    return 0;
}
