/**
 * @file
 * @brief Inquiries a running acquisition has to refuse.
 *
 * The readout measures the sensor temperature by way of the DAC scan: it
 * reloads every sensor register from its own image, points the sense-DAC
 * selector at the temperature DACs, and flushes the lot to the sensor. Doing
 * that mid-acquisition pushes whatever registers were written since the last
 * flush -- the pixel mode and the fast-oscillator flag among them -- so the
 * sensor can change pixel format in the middle of the stream. The all-DAC
 * scan runs the same routine twenty-two times over. Both are therefore
 * refused while a measurement is in flight; the readout-side inquiries, which
 * read an ADC or a board sensor and never reach the chip, stay available.
 *
 * These cases need no readout, emulated or otherwise: the guard sits ahead of
 * the session lock and of any I/O, so a device whose sockets were never
 * initialized is enough to exercise it. That is deliberate -- it also proves
 * the guard really does come first, since a guard placed after the lock would
 * hang or crash here instead of returning.
 *
 * @author Petr Mánek
 * @date 27.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

static void
test_sensor_temperature_refused_while_running(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    float temperature = 0.0f;

    /* Idle devices are not exercised here: with the guard passed, the call
       would go on to lock and use sockets this device does not have. Only the
       refusal is reachable without a readout, and only the refusal is the
       behaviour under test. */
    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    device.acquisition = &acq;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, &temperature), -KATHERINE_E_STATE);

    /* The out parameter is left alone when the call is refused. */
    KT_CHECK_EXACT(temperature, 0.0f);
}

static void
test_guard_is_the_first_thing_the_call_does(void)
{
    /* A device that is all zeroes has no usable sockets, so reaching the lock
       at all would be observable as something other than a clean return. Two
       further refusals, to pin that the guard does not depend on any field
       having been initialized. */
    katherine_device_t device;
    katherine_acquisition_t acq;
    memset(&device, 0, sizeof(device));
    memset(&acq, 0, sizeof(acq));
    device.acquisition = &acq;

    float first = 0.0f, second = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, &first), -KATHERINE_E_STATE);
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, &second), -KATHERINE_E_STATE);
}

/* A begin that fails must not leave the device claiming a measurement. The
   flag is set on the success path only, after the start command is away, so
   every early return out of katherine_acquisition_begin() -- the argument
   check here, and equally the session lock and the start command further down,
   which this test cannot provoke without a readout -- leaves the device idle.
   Were it set before those, a caller whose begin failed would be refused
   inquiries indefinitely: it has no reason to call read(), and read() is what
   clears the flag. */
static void
test_failed_begin_leaves_the_device_idle(void)
{
    katherine_device_t device;
    katherine_acquisition_t acq;
    katherine_config_t config;

    memset(&device, 0, sizeof(device));
    memset(&acq, 0, sizeof(acq));
    memset(&config, 0, sizeof(config));

    acq.device = &device;

    /* Data-driven readout with more than one frame is rejected before any
       socket is touched, which is what makes this reachable with a device
       that has none. */
    config.no_frames = 2;

    /* Fast time stamping is left off deliberately. A zeroed config selects
       whichever frequency is enumerated first, which need not be one where
       fine time stamping is coherent; asking for it could then trip that guard
       instead, and this test would pass while exercising something other than
       the frame-count check it names. Switching it off decouples the two
       without pinning a frequency here. */
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, &config, READOUT_DATA_DRIVEN,
                    ACQUISITION_MODE_TOA_TOT, false, true),
        -KATHERINE_E_INVAL);
    KT_CHECK(device.acquisition == NULL);
}

/* Asking for fine time stamping where it is not coherent is a configuration
   error, refused rather than turned into a stream whose Timestamp = ToA - fToA
   has no meaning. Reachable without a readout for the same reason as the checks
   above: the guard precedes every socket.

   The expectation is derived from katherine_freq_is_fast_vco_supported rather
   than from a list of frequencies written out here, so this keeps testing that
   the guard agrees with the predicate instead of testing a frozen copy of the
   predicate's current answer. */
static void
test_fast_vco_refused_where_incoherent(void)
{
    katherine_device_t device;
    katherine_acquisition_t acq;
    katherine_config_t config;

    const katherine_freq_t all[] = {FREQ_20, FREQ_40, FREQ_80, FREQ_160};
    unsigned refused             = 0;

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        /* Coherent frequencies are skipped rather than asserted: with the
           guard passed, begin() would go on to sockets a zeroed device does
           not have. */
        if (katherine_freq_is_fast_vco_supported(all[i])) continue;

        memset(&device, 0, sizeof(device));
        memset(&acq, 0, sizeof(acq));
        memset(&config, 0, sizeof(config));
        acq.device       = &device;
        config.no_frames = 1;
        config.freq      = all[i];

        KT_CHECK_EQ(katherine_acquisition_begin(&acq, &config, READOUT_SEQUENTIAL,
                        ACQUISITION_MODE_TOA_TOT, true, true),
            -KATHERINE_E_INVAL);
        KT_CHECK(device.acquisition == NULL);
        ++refused;
    }

    /* A derived loop can pass by running zero times. If every frequency ever
       becomes coherent, this test has nothing left to say and should fail
       loudly rather than go green in silence. */
    KT_CHECK(refused > 0);
}

/* The mirror case -- that slow time stamping is accepted at every frequency --
   is not testable here: with the guard passed, begin() goes on to the sockets
   a zeroed device does not have. The predicate itself is covered by
   test_clock_phases.c. */

int
main(void)
{
    KT_RUN(test_sensor_temperature_refused_while_running);
    KT_RUN(test_guard_is_the_first_thing_the_call_does);
    KT_RUN(test_failed_begin_leaves_the_device_idle);
    KT_RUN(test_fast_vco_refused_where_incoherent);
    return kt_summary();
}
