/**
 * \file
 * \brief Every entry point, on a device that has no link.
 *
 * katherine_device_init() zeroes the device before the first step that can
 * fail, so a caller that ignores its error and carries on holds a device
 * whose every transport operation dereferences a null vtable. Twenty-one of
 * the thirty-nine entry points below used to segfault on one -- eleven
 * straight away, and ten more once katherine_device_declare() had satisfied
 * the katherine_device_derived_info_t::supported guard they relied on.
 *
 * Nothing here needs a readout: the calls must all refuse before they reach
 * a link. A call that crashes takes the whole binary with it, which is the
 * failure this file exists to report.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

static katherine_device_t device;
static char scratch[256];
static float value;
static katherine_comm_status_t comm_status;
static katherine_gpio_config_t gpio_config;
static katherine_gpio_monitor_t gpio_monitor;
static katherine_pulse_config_t pulse_config;
static katherine_pulse_counts_t pulse_counts;
static katherine_readout_status_t readout_status;
static katherine_tdc_config_t tdc_config;
static katherine_tdc_counts_t tdc_counts;
static katherine_tpx3_config_check_t tpx3_config_check;
static katherine_tpx3_config_t tpx3_config;
static katherine_tpx3_dac_voltages_t tpx3_dac_voltages;
static katherine_tpx3_dacs_t tpx3_dacs;
static katherine_tpx3_px_config_t tpx3_px_config;
static katherine_tpx3_test_pulse_config_t tpx3_test_pulse_config;
static katherine_trigger_t trigger;

// ------------------------------------------------------------------

// Which code each one refuses with is its own business -- a chip command
// answers KATHERINE_E_BAD_CHIP, a readout command KATHERINE_E_STATE. What
// every one of them owes the caller is an answer rather than a signal, and
// never success.
static void
every_call_refuses(void)
{
    KT_CHECK(katherine_tpx3_configure(&device, &tpx3_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_verify_config(&device, 0, &tpx3_config_check) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_all_pixel_config(&device, 0, &tpx3_px_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_acq_time(&device, 0.0f) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_acq_mode(&device, (katherine_tpx3_px_mode_t) 0, false) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_frame_count(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_token_count(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_bias(&device, 0, 0.0f) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_seq_readout_start(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_acquisition_triggers(&device, &trigger, &trigger) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_sensor_register(&device, 0, (katherine_tpx3_reg_t) 0, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_update_sensor_registers(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_output_block_config_update(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_timer_set(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_dac(&device, 0, (katherine_tpx3_dac_t) 0, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_dacs(&device, 0, &tpx3_dacs) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_set_test_pulses(&device, &tpx3_test_pulse_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_device_select_interface(&device) != KATHERINE_E_OK);
    KT_CHECK(katherine_device_enumerate(&device) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_gpio_config(&device, &gpio_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_composed_ready_signal(&device, &gpio_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_probe_gpio(&device, &gpio_monitor) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_pulse_generator(&device, &pulse_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_pulse_generator_counts(&device, &pulse_counts) != KATHERINE_E_OK);
    KT_CHECK(katherine_reset_pulse_generator_counts(&device) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_readout_status(&device, &readout_status) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_comm_status(&device, &comm_status) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_chip_id(&device, 0, (char *) scratch) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_readout_temperature(&device, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_sensor_temperature(&device, 0, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_perform_digital_test(&device, 0) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_adc_voltage(&device, 0, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_bias(&device, 0, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_bias_leakage(&device, 0, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_get_dac_monitor_voltage(&device, 0, (katherine_tpx3_dac_monitor_t) 0, &value) != KATHERINE_E_OK);
    KT_CHECK(katherine_tpx3_get_dac_monitor_voltages(&device, 0, &tpx3_dac_voltages) != KATHERINE_E_OK);
    KT_CHECK(katherine_set_tdc_config(&device, &tdc_config) != KATHERINE_E_OK);
    KT_CHECK(katherine_get_tdc_counts(&device, &tdc_counts) != KATHERINE_E_OK);
}

static void
test_a_device_that_was_never_opened_refuses_everything(void)
{
    memset(&device, 0, sizeof(device));
    every_call_refuses();
}

// katherine_device_declare() writes the derived record without touching a
// link, so it is the one way to hold a device that reads as supported and
// has nothing to talk to.
static void
test_a_declared_device_with_no_link_refuses_everything(void)
{
    const katherine_device_info_t info = {
        .hw_type       = 2,
        .hw_revision   = 3,
        .serial_number = 1,
        .fw_version    = 5,
        .chip_count    = 1,
    };

    memset(&device, 0, sizeof(device));
    KT_REQUIRE(katherine_device_declare(&device, &info) == KATHERINE_E_OK);
    every_call_refuses();
}

int
main(void)
{
    KT_RUN(test_a_device_that_was_never_opened_refuses_everything);
    KT_RUN(test_a_declared_device_with_no_link_refuses_everything);
    return kt_summary();
}
