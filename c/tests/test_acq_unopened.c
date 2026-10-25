/**
 * \file
 * \brief Every acquisition entry point, on an acquisition that holds no device.
 *
 * katherine_acquisition_fini() releases the device by setting the pointer to
 * NULL, which is what keeps it from dangling -- and left every other entry
 * point dereferencing it. The same hole swallowed an acquisition that was
 * never initialized at all, because
 * KATHERINE_ACQUISITION_STATE_NOT_STARTED is zero and a zeroed object
 * therefore reads as one ready to begin.
 *
 * Four of the seven used to segfault in both states. Nothing here needs a
 * readout: every call must refuse before it reaches one.
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

static katherine_acquisition_t acq;
static katherine_device_t device;
static katherine_tpx3_config_t config;
static char scratch[512];

// ------------------------------------------------------------------

// snprint and the phase offset read fields rather than the device, so they
// answer in any state; fini is the one call that is defined on a spent
// acquisition.
static void
every_call_refuses(void)
{
    KT_CHECK(katherine_acquisition_snprint(scratch, sizeof(scratch), &acq) > 0);
    (void) katherine_acquisition_timestamp_phase_offset(&acq, (katherine_tpx3_coord_t) {0, 0});

    KT_CHECK(katherine_acquisition_begin(&acq, &config, true) == KATHERINE_E_STATE);
    KT_CHECK(katherine_acquisition_abort(&acq) == KATHERINE_E_STATE);
    KT_CHECK(katherine_acquisition_stop(&acq) == KATHERINE_E_STATE);
    KT_CHECK(katherine_acquisition_read(&acq) == KATHERINE_E_STATE);

    katherine_acquisition_fini(&acq);
}

static void
test_an_acquisition_that_was_never_initialized_refuses_everything(void)
{
    memset(&acq, 0, sizeof(acq));
    every_call_refuses();
}

// An init against a device with no link cannot open a data transport, so it
// fails -- and leaves the acquisition holding the device it was given.
static void
test_an_acquisition_whose_init_failed_refuses_everything(void)
{
    memset(&acq, 0, sizeof(acq));
    memset(&device, 0, sizeof(device));
    KT_REQUIRE(katherine_acquisition_init(&acq, &device, NULL, 65536, 4096, 10, 10) != KATHERINE_E_OK);
    every_call_refuses();
}

// And once finalized the acquisition is spent, which is the state a caller
// reaches by cleaning up and then stopping from another thread.
static void
test_a_finalized_acquisition_refuses_everything(void)
{
    memset(&acq, 0, sizeof(acq));
    memset(&device, 0, sizeof(device));
    (void) katherine_acquisition_init(&acq, &device, NULL, 65536, 4096, 10, 10);
    katherine_acquisition_fini(&acq);
    every_call_refuses();
}

int
main(void)
{
    KT_RUN(test_an_acquisition_that_was_never_initialized_refuses_everything);
    KT_RUN(test_an_acquisition_whose_init_failed_refuses_everything);
    KT_RUN(test_a_finalized_acquisition_refuses_everything);
    return kt_summary();
}
