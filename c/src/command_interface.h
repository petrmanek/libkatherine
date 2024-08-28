/* Katherine Control Library
 *
 * This file was created on 9.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once 

#include <katherine/global.h>
#include <katherine/udp.h>

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

static inline int
katherine_cmd_wait_ack_crd(katherine_udp_t* udp, char *ack)
{
    int res;

    res = katherine_udp_recv_exact(udp, ack, 8);
    if (res) goto err;

    // TODO: optionally check command response id
    return 0;

err:
    return res;
}

static inline int
katherine_cmd_wait_ack(katherine_udp_t* udp)
{
    char ack[8];
    return katherine_cmd_wait_ack_crd(udp, ack);
}

static inline int
katherine_cmd(katherine_udp_t* udp, const void* buffer, size_t count)
{
    int res;

    res = katherine_udp_send_exact(udp, buffer, count);
    if (res) goto err;

    return 0;

err:
    return res;
}

static inline int
katherine_cmd6(katherine_udp_t *udp, char val6)
{
    char cmd[8] = {0};
    cmd[6] = val6;
    return katherine_cmd(udp, &cmd, sizeof(cmd));
}

static inline int
katherine_cmd60(katherine_udp_t *udp, char val6, char val0)
{
    char cmd[8] = {0};
    cmd[6] = val6;
    cmd[0] = val0;
    return katherine_cmd(udp, &cmd, sizeof(cmd));
}

static inline void
katherine_cmd_long(char *cmd, long value)
{
    int i = 0;
    int mlsb, mmsb;
    while (value > 0) {
        mlsb = (0x0f) & (int) (value % 16);
        value /= 16;

        mmsb = (0x0f) & (int) (value % 16);
        value /= 16;

        cmd[i++] = (char) ((0xff & mlsb) | (0xff & mmsb) << 4);
    }
}

static inline int
katherine_cmd64_long(katherine_udp_t *udp, char val6, char val4, long value)
{
    char cmd[8] = {0};
    cmd[6] = val6;
    cmd[4] = val4;
    katherine_cmd_long(cmd, value);

    return katherine_cmd(udp, &cmd, sizeof(cmd));
}

static inline int
katherine_cmd6_long(katherine_udp_t *udp, char val6, long value)
{
    char cmd[8] = {0};
    cmd[6] = val6;
    katherine_cmd_long(cmd, value);

    return katherine_cmd(udp, &cmd, sizeof(cmd));
}

static inline int
katherine_cmd6_float(katherine_udp_t* udp, char val6, float value)
{
    char cmd[8] = {0};
    cmd[6] = val6;
    int *bits = (int*) &value;
    cmd[0] = (*bits & 0xff);
    cmd[1] = ((*bits >> 8) & 0xff);
    cmd[2] = ((*bits >> 16) & 0xff);
    cmd[3] = ((*bits >> 24) & 0xff);

    return katherine_cmd(udp, &cmd, sizeof(cmd));
}

#define K_DEFINE_CMD_ARG0(A, CMD_NAME, ...) \
    static inline int katherine_cmd_##CMD_NAME(katherine_udp_t *udp) \
    {\
        return katherine_##A(udp, __VA_ARGS__);\
    }

#define K_DEFINE_CMD_ARG1(A, CMD_NAME, ARG1_TYPE, ...) \
    static inline int katherine_cmd_##CMD_NAME(katherine_udp_t *udp, ARG1_TYPE arg1) \
    {\
        return katherine_##A(udp, __VA_ARGS__, arg1);\
    }

typedef enum katherine_cmd_type {
    CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB  = 0x01,
    CMD_TYPE_BIAS_SETTINGS                  = 0x02,
    CMD_TYPE_ACQUISITION_START              = 0x03,
    CMD_TYPE_INTERNAL_DAC_SETTINGS          = 0x04,
    CMD_TYPE_SEQ_READOUT_START              = 0x05,
    CMD_TYPE_ACQUISITION_STOP               = 0x06,
    CMD_TYPE_HW_COMMAND_START               = 0x07,
    CMD_TYPE_SENSOR_REGISTER_SETTING        = 0x08,
    CMD_TYPE_ACQUISITION_MODE_SETTING       = 0x09,
    CMD_TYPE_ACQUISITION_TIME_SETTING_MSB   = 0x0A,
    CMD_TYPE_ECHO_CHIP_ID                   = 0x0B,
    CMD_TYPE_GET_BIAS_VOLTAGE               = 0x0C,
    CMD_TYPE_GET_ADC_VOLTAGE                = 0x0D,
    CMD_TYPE_GET_BACK_READ_REGISTER         = 0x0E,
    CMD_TYPE_INTERNAL_DAC_SCAN              = 0x0F,
    CMD_TYPE_SET_PIXEL_CONFIG               = 0x10,
    CMD_TYPE_GET_PIXEL_CONFIG               = 0x11,
    CMD_TYPE_SET_ALL_PIXEL_CONFIG           = 0x12,
    CMD_TYPE_NUMBER_OF_FRAMES               = 0x13,
    CMD_TYPE_GET_ALL_DAC_SCAN               = 0x14,
    CMD_TYPE_GET_HW_READOUT_TEMPERATURE     = 0x15,
    CMD_TYPE_LED_SETTINGS                   = 0x16,
    CMD_TYPE_GET_READOUT_STATUS             = 0x17,
    CMD_TYPE_GET_COMMUNICATION_STATUS       = 0x18,
    CMD_TYPE_GET_SENSOR_TEMPERATURE         = 0x19,
    CMD_TYPE_DIGITAL_TEST                   = 0x20,
    CMD_TYPE_ACQUISITION_SETUP              = 0x21,
    CMD_TYPE_INTERNAL_TRIGGER_GENERATOR     = 0x23,
    CMD_TYPE_TOA_CALIBRATION_SETUP          = 0x28,
    CMD_TYPE_INTERNAL_TDC_SETTINGS          = 0x32,
} katherine_cmd_type_t;

K_DEFINE_CMD_ARG0(cmd6,       set_all_pixel_config,                     CMD_TYPE_SET_ALL_PIXEL_CONFIG)
K_DEFINE_CMD_ARG0(cmd6,       echo_chip_id,                             CMD_TYPE_ECHO_CHIP_ID)
K_DEFINE_CMD_ARG0(cmd6,       get_readout_temperature,                  CMD_TYPE_GET_HW_READOUT_TEMPERATURE)
K_DEFINE_CMD_ARG0(cmd6,       get_sensor_temperature,                   CMD_TYPE_GET_SENSOR_TEMPERATURE)
K_DEFINE_CMD_ARG0(cmd6,       get_readout_status,                       CMD_TYPE_GET_READOUT_STATUS)
K_DEFINE_CMD_ARG0(cmd6,       get_comm_status,                          CMD_TYPE_GET_COMMUNICATION_STATUS)
K_DEFINE_CMD_ARG0(cmd6,       digital_test,                             CMD_TYPE_DIGITAL_TEST)

typedef enum katherine_hw_cmd_type {
    CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE            = 0,
    CMD_START_INTERNAL_DAC_UPDATE                       = 1,
    CMD_START_INTERNAL_DAC_BACK_READ                    = 2,
    CMD_START_TIMER_READ                                = 3,
    CMD_START_TIMER_SET                                 = 4,
    CMD_START_RESET_MATRIX_SEQUENTIAL                   = 5,
    CMD_START_STOP_MATRIX_COMMAND                       = 6,
    CMD_START_LOAD_COLUMN_TEST_PULSE_REGISTER           = 7,
    CMD_START_READ_COLUMN_TEST_PULSE_REGISTER           = 8,
    CMD_START_LOAD_PIXEL_REGISTER_CONFIGURATION         = 9,
    CMD_START_READ_PIXEL_REGISTER_CONFIGURATION         = 10,
    CMD_START_READ_PIXEL_MATRIX_SEQUENTIAL              = 11,
    CMD_START_READ_PIXEL_MATRIX_DATA_DRIVEN_SETTING     = 12,
    CMD_START_CHIP_ID_READ                              = 13,
    CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE                = 14,
    CMD_START_DIGITAL_TEST                              = 15,
} katherine_hw_cmd_type_t;

K_DEFINE_CMD_ARG0(cmd60,      hw_sensor_config_registers_update,        CMD_TYPE_HW_COMMAND_START, CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE)
K_DEFINE_CMD_ARG0(cmd60,      hw_internal_dac_update,                   CMD_TYPE_HW_COMMAND_START, CMD_START_INTERNAL_DAC_UPDATE)
K_DEFINE_CMD_ARG0(cmd60,      hw_internal_dac_back_read,                CMD_TYPE_HW_COMMAND_START, CMD_START_INTERNAL_DAC_BACK_READ)
K_DEFINE_CMD_ARG0(cmd60,      hw_timer_read,                            CMD_TYPE_HW_COMMAND_START, CMD_START_TIMER_READ)
K_DEFINE_CMD_ARG0(cmd60,      hw_timer_set,                             CMD_TYPE_HW_COMMAND_START, CMD_START_TIMER_SET)
K_DEFINE_CMD_ARG0(cmd60,      hw_reset_matrix_sequential,               CMD_TYPE_HW_COMMAND_START, CMD_START_RESET_MATRIX_SEQUENTIAL)
K_DEFINE_CMD_ARG0(cmd60,      hw_stop_matrix_command,                   CMD_TYPE_HW_COMMAND_START, CMD_START_STOP_MATRIX_COMMAND)
K_DEFINE_CMD_ARG0(cmd60,      hw_load_column_test_pulse_register,       CMD_TYPE_HW_COMMAND_START, CMD_START_LOAD_COLUMN_TEST_PULSE_REGISTER)
K_DEFINE_CMD_ARG0(cmd60,      hw_read_column_test_pulse_register,       CMD_TYPE_HW_COMMAND_START, CMD_START_READ_COLUMN_TEST_PULSE_REGISTER)
K_DEFINE_CMD_ARG0(cmd60,      hw_load_pixel_register_configuration,     CMD_TYPE_HW_COMMAND_START, CMD_START_LOAD_PIXEL_REGISTER_CONFIGURATION)
K_DEFINE_CMD_ARG0(cmd60,      hw_read_pixel_register_configuration,     CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_REGISTER_CONFIGURATION)
K_DEFINE_CMD_ARG0(cmd60,      hw_read_pixel_matrix_sequential_setting,  CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_MATRIX_SEQUENTIAL)
K_DEFINE_CMD_ARG0(cmd60,      hw_read_pixel_matrix_data_driven_setting, CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_MATRIX_DATA_DRIVEN_SETTING)
K_DEFINE_CMD_ARG0(cmd60,      hw_chip_id_read,                          CMD_TYPE_HW_COMMAND_START, CMD_START_CHIP_ID_READ)
K_DEFINE_CMD_ARG0(cmd60,      hw_output_block_config_update,            CMD_TYPE_HW_COMMAND_START, CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE)
K_DEFINE_CMD_ARG0(cmd60,      hw_digital_test,                          CMD_TYPE_HW_COMMAND_START, CMD_START_DIGITAL_TEST)

K_DEFINE_CMD_ARG1(cmd6_long,  set_acqtime_lsb,                          long, CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB)
K_DEFINE_CMD_ARG1(cmd6_long,  set_acqtime_msb,                          long, CMD_TYPE_ACQUISITION_TIME_SETTING_MSB)
K_DEFINE_CMD_ARG1(cmd6_long,  set_number_of_frames,                     long, CMD_TYPE_NUMBER_OF_FRAMES)
K_DEFINE_CMD_ARG1(cmd6_long,  set_seq_readout_start,                    long, CMD_TYPE_SEQ_READOUT_START)
K_DEFINE_CMD_ARG1(cmd6_long,  start_acquisition,                        char, CMD_TYPE_ACQUISITION_START)
K_DEFINE_CMD_ARG1(cmd6_long,  stop_acquisition,                         char, CMD_TYPE_ACQUISITION_STOP)
K_DEFINE_CMD_ARG1(cmd6_long,  get_adc_voltage,                          unsigned char, CMD_TYPE_GET_ADC_VOLTAGE)
K_DEFINE_CMD_ARG1(cmd6_float, set_bias_settings,                        float, CMD_TYPE_BIAS_SETTINGS)

K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_preamp_on,                  long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 0)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_preamp_off,                 long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 1)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vpreamp_ncas,                     long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 2)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_ikrum,                      long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 3)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vfbk,                             long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 4)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vthreshold_fine,                  long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 5)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vthreshold_coarse,                long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 6)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_discs1_on,                  long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 7)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_discs1_off,                 long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 8)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_discs2_on,                  long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 9)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_discs2_off,                 long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 10)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_pixeldac,                   long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 11)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_tpbufferin,                 long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 12)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_tpbufferout,                long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 13)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vtp_coarse,                       long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 14)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_vtp_fine,                         long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 15)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_ibias_cp_pll,                     long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 16)
K_DEFINE_CMD_ARG1(cmd64_long, set_dac_pll_vcntrl,                       long, CMD_TYPE_INTERNAL_DAC_SETTINGS, 17)

#undef K_DEFINE_CMD_ARG0
#undef K_DEFINE_CMD_ARG1

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
