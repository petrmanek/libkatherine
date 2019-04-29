/* Katherine Control Library
 *
 * This file was created on 14.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#include <katherine/config.h>
#include <katherine/device.h>
#include "command_interface.h"

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

    int32_t general_setup = 0x58;
    general_setup |= !config->polarity_holes;
    general_setup |= (config->gray_disable ? 1 : 0) << 3;
    res = katherine_set_sensor_register(device, TPX3_REG_GENERAL_CONFIG, general_setup);
    if (res) goto err;

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

    return 0;

err:
    return res;
}

/**
 * Configure all pixels of the detector.
 * @param device Katherine device
 * @param px_config Configuration matrix to set
 * @return Error code.
 */
int
katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t* px_config)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err;

    // Send command.
    res = katherine_cmd_set_all_pixel_config(&device->control_socket);
    if (res) goto err;

    // Send pixel configuration data.
    const char *config = (const char *) px_config->words;
    for (int i = 0; i < 64; ++i) {
        res = katherine_cmd(&device->control_socket, config + 1024 * i, 1024);
        if (res) goto err;
    }

    // Wait for acknowledgement.
    res = katherine_cmd_wait_ack(&device->control_socket);
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
    if (res) goto err;

    long acqt = (long) (ns / 10.);
    long lsb = (acqt & 0x00000000FFFFFFFF);
    long msb = (acqt & 0xFFFFFFFF00000000) >> 32;

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
    if (res) goto err;

    char cmd[8] = {0};
    cmd[6] = CMD_TYPE_ACQUISITION_MODE_SETTING;

    cmd[0] |= acq_mode;
    cmd[0] |= fast_vco_enabled << 7;

    res = katherine_cmd(&device->control_socket, &cmd, sizeof(cmd));
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
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err;

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
    if (res) goto err;

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
    if (res) goto err;

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
    if (res) goto err;

    char cmd[8] = {0};
    cmd[6] = CMD_TYPE_ACQUISITION_SETUP;
    cmd[4] = 0x05;

    cmd[0] |= start_trigger->enabled;
    cmd[0] |= start_trigger->channel << 1;
    cmd[0] |= start_trigger->use_falling_edge << 4;
    cmd[0] |= delayed_start << 5;

    cmd[1] |= end_trigger->enabled;
    cmd[1] |= end_trigger->channel << 1;
    cmd[1] |= end_trigger->use_falling_edge << 4;

    res = katherine_cmd(&device->control_socket, &cmd, sizeof(cmd));
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
    if (res) goto err;

    char cmd[8] = {0};
    cmd[6] = CMD_TYPE_SENSOR_REGISTER_SETTING;
    cmd[4] = reg_idx;
    katherine_cmd_long(cmd, reg_value);

    res = katherine_cmd(&device->control_socket, &cmd, sizeof(cmd));
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
    if (res) goto err;

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
    if (res) goto err;

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
    if (res) goto err;

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
 * @param device Katherine device
 * @param dacs DAC register values to set
 * @return Error code.
 */
int
katherine_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs)
{
    int res;

    res = katherine_udp_mutex_lock(&device->control_socket);
    if (res) goto err;

    for (int i = 0; i < 18; ++i) {
        char cmd[8] = {0};
        cmd[6] = CMD_TYPE_INTERNAL_DAC_SETTINGS;
        cmd[4] = (char) i;
        katherine_cmd_long(cmd, dacs->array[i]);

        res = katherine_cmd(&device->control_socket, &cmd, sizeof(cmd));
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
