/**
 * \file
 * \brief Timepix3 commands, refused on a readout that carries something else.
 *
 * The device table recognises a Katherine for Timepix4 and records that its
 * chip is not a Timepix3. Until now nothing read that: every
 * katherine_tpx3_* entry point checked the chip index and went to the wire,
 * so pointing this library at such a readout put Timepix3 opcodes on it.
 *
 * They do not bounce. That firmware reads `0x05` as "write one DAC to the
 * chip" where this library means "start the readout", and waits for 229376
 * bytes of pixel matrix where this one sends 65536 -- so the outcomes are a
 * misconfigured chip and a readout left waiting, neither of which reports
 * anything.
 *
 * Every entry point is listed below rather than a representative few. The
 * guard sits in the lock each of them takes, so one missing call here is a
 * command that would still reach the wire, and the way this defect arose in
 * the first place was a list built from the calls somebody thought of.
 *
 * No readout is needed: the refusal precedes the link, which is what the
 * second half of each case asserts -- a device with no transport reports the
 * ASIC rather than the missing link.
 *
 * \author Petr Mánek
 * \date 29.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

// The hardware type of a Katherine for Timepix4, as the device table lists
// it. Declared rather than enumerated, so no readout is contacted.
#define HW_TYPE_TPX4 0x0A
#define HW_TYPE_TPX3 0x03

static void
declare(katherine_device_t *device, uint8_t hw_type)
{
    memset(device, 0, sizeof(*device));

    const katherine_device_info_t info = {.status = {.hw_type = hw_type}, .chip_count = 1};
    KT_REQUIRE(katherine_device_declare(device, &info) == KATHERINE_E_OK);
}

// Every katherine_tpx3_* entry point that reaches the control plane, called
// on one device. The return code is all that is checked: none of these can
// proceed, because the device has no link.
#define FOR_EVERY_TPX3_COMMAND(device, CHECK) \
    do { \
        float voltage = 0.0f; \
        katherine_tpx3_dac_voltages_t voltages; \
        katherine_tpx3_dacs_t dacs; \
        katherine_tpx3_config_check_t check; \
        katherine_tpx3_test_pulse_config_t tp; \
        katherine_tpx3_px_config_t *px = (katherine_tpx3_px_config_t *) calloc(1, PX_CONFIG_BYTES); \
        KT_REQUIRE(px != NULL); \
        memset(&voltages, 0, sizeof(voltages)); \
        memset(&dacs, 0, sizeof(dacs)); \
        memset(&check, 0, sizeof(check)); \
        memset(&tp, 0, sizeof(tp)); \
\
        CHECK(katherine_tpx3_perform_digital_test((device), 0)); \
        CHECK(katherine_tpx3_get_dac_monitor_voltage( \
            (device), 0, KATHERINE_TPX3_DAC_MONITOR_BANDGAP_OUTPUT, &voltage)); \
        CHECK(katherine_tpx3_get_dac_monitor_voltages((device), 0, &voltages)); \
        CHECK(katherine_tpx3_verify_config((device), 0, &check)); \
        CHECK(katherine_tpx3_set_all_pixel_config((device), 0, px)); \
        CHECK(katherine_tpx3_set_acq_mode((device), KATHERINE_TPX3_PX_TOA_TOT, true)); \
        CHECK(katherine_tpx3_set_token_count((device), 17)); \
        CHECK(katherine_tpx3_start_readout((device), KATHERINE_TPX3_READOUT_SEQUENTIAL)); \
        CHECK(katherine_tpx3_set_sensor_register((device), 0, 0, 0)); \
        CHECK(katherine_tpx3_update_sensor_registers((device), 0)); \
        CHECK(katherine_tpx3_output_block_config_update((device), 0)); \
        CHECK(katherine_tpx3_timer_set((device), 0)); \
        CHECK(katherine_tpx3_set_dac((device), 0, 0, 0)); \
        CHECK(katherine_tpx3_set_dacs((device), 0, &dacs)); \
        CHECK(katherine_tpx3_set_test_pulses((device), &tp)); \
\
        free(px); \
    } while (0)

#define PX_CONFIG_BYTES              (sizeof(katherine_tpx3_px_config_t))

#define EXPECT_UNSUPPORTED(call)     KT_CHECK_EQ((call), KATHERINE_E_UNSUPPORTED)
#define EXPECT_NOT_UNSUPPORTED(call) KT_CHECK((call) != KATHERINE_E_UNSUPPORTED)

// ------------------------------------------------------------------

static void
test_a_timepix4_readout_refuses_every_timepix3_command(void)
{
    katherine_device_t device;
    declare(&device, HW_TYPE_TPX4);
    KT_REQUIRE(device.derived_info.chip_type == KATHERINE_CHIP_TPX4);

    FOR_EVERY_TPX3_COMMAND(&device, EXPECT_UNSUPPORTED);
}

// The same calls on a Timepix3 readout get past the ASIC question and fail
// for their own reasons -- here, a device with no link. Without this the
// case above would pass against a guard that refused everything.
static void
test_a_timepix3_readout_is_not_refused_for_its_asic(void)
{
    katherine_device_t device;
    declare(&device, HW_TYPE_TPX3);
    KT_REQUIRE(device.derived_info.chip_type == KATHERINE_CHIP_TPX3);

    FOR_EVERY_TPX3_COMMAND(&device, EXPECT_NOT_UNSUPPORTED);
}

// A device nobody has enumerated says nothing about its chip, and is left as
// it was: the guard refuses a positive identification of something else, not
// an absence of one.
static void
test_an_unenumerated_device_is_left_alone(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));
    KT_REQUIRE(device.derived_info.chip_type == KATHERINE_CHIP_UNKNOWN);

    FOR_EVERY_TPX3_COMMAND(&device, EXPECT_NOT_UNSUPPORTED);
}

int
main(void)
{
    KT_RUN(test_a_timepix4_readout_refuses_every_timepix3_command);
    KT_RUN(test_a_timepix3_readout_is_not_refused_for_its_asic);
    KT_RUN(test_an_unenumerated_device_is_left_alone);
    return kt_summary();
}
