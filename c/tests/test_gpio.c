/**
 * \file
 * \brief The GPIO channels: what they can be set to, and what a probe reports.
 *
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

// The readout multiplexes a mapping onto an output channel by its value
// minus one, in a five-bit field, so the last mapping has to be the widest
// value that field holds. A mapping added past it would silently truncate.
_Static_assert(KATHERINE_GPIO_HIGH == 32, "the mapping enumeration must end at the constant high");

// A period the tick unit divides exactly, so the expected frequency is a
// round number: half a million ticks of 10 ns is 5 ms, hence 200 Hz.
#define HIGH_TICKS 300000u
#define LOW_TICKS  200000u
#define PERIOD_HZ  200.0
#define DUTY       0.6

// ------------------------------------------------------------------

// a) Every mapping has a name of its own, which is what makes a value read
// back off a readout reportable.
static void
test_a_every_mapping_is_named(void)
{
    for (unsigned m = 0; m <= KATHERINE_GPIO_HIGH; ++m) {
        const char *name = katherine_str_gpio_mapping((katherine_gpio_mapping_t) m);

        KT_CHECK(strcmp(name, "unknown") != 0);

        // And no two of them share a name.
        for (unsigned n = 0; n < m; ++n) {
            KT_CHECK(strcmp(name, katherine_str_gpio_mapping((katherine_gpio_mapping_t) n)) != 0);
        }
    }

    KT_CHECK_STR_EQ(katherine_str_gpio_mapping((katherine_gpio_mapping_t) (KATHERINE_GPIO_HIGH + 1)),
        "unknown");
    KT_CHECK_STR_EQ(katherine_str_gpio_mapping((katherine_gpio_mapping_t) -1), "unknown");

    // The three names a probe reports are distinct too.
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal(KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT), "constant");
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal(KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC), "periodic");
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal((katherine_gpio_monitor_signal_t) 99), "unknown");
}

// b) Frequency and duty cycle follow from the two tick counts, and are zero
// where the readout measured no period rather than dividing by it.
static void
test_b_frequency_and_duty_come_from_the_tick_counts(void)
{
    katherine_gpio_channel_monitor_t v = {
        .level      = true,
        .signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC,
        .high_ticks = HIGH_TICKS,
        .low_ticks  = LOW_TICKS,
    };

    KT_CHECK_NEAR(katherine_gpio_channel_frequency(&v), PERIOD_HZ, 1e-9);
    KT_CHECK_NEAR(katherine_gpio_channel_duty_cycle(&v), DUTY, 1e-9);

    // A channel the readout does not time carries counts that mean nothing,
    // so neither is reported.
    v.signal = KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN;
    KT_CHECK_EQ(katherine_gpio_channel_frequency(&v), 0);
    KT_CHECK_EQ(katherine_gpio_channel_duty_cycle(&v), 0);

    // And a period of zero ticks is not divided by.
    v.signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC;
    v.high_ticks = 0;
    v.low_ticks  = 0;
    KT_CHECK_EQ(katherine_gpio_channel_frequency(&v), 0);
    KT_CHECK_EQ(katherine_gpio_channel_duty_cycle(&v), 0);
}

int
main(void)
{
    KT_RUN(test_a_every_mapping_is_named);
    KT_RUN(test_b_frequency_and_duty_come_from_the_tick_counts);
    return kt_summary();
}
