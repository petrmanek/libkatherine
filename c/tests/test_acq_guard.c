/**
 * \file
 * \brief Inquiries a running acquisition has to refuse.
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
 * \author Petr Mánek
 * \date 27.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#include "atomic.h"
#include "acq/private.h"
#include "ktest.h"


// A structure with a flexible array member cannot be a member of another, so
// one chip's worth of configuration is allocated rather than declared.
#define CONFIG_SIZE_1 (sizeof(katherine_tpx3_config_t) + sizeof(katherine_tpx3_chip_config_t))

static katherine_tpx3_config_t *
alloc_config_1(void)
{
    return (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE_1);
}

static void
test_sensor_temperature_refused_while_running(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    float temperature = 0.0f;

    // Idle devices are not exercised here: with the guard passed, the call
    // would go on to lock and use sockets this device does not have. Only the
    // refusal is reachable without a readout, and only the refusal is the
    // behaviour under test.
    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    device.acquisition = &acq;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, 0, &temperature), KATHERINE_E_STATE);

    // The out parameter is left alone when the call is refused.
    KT_CHECK_EXACT(temperature, 0.0f);
}

static void
test_guard_is_the_first_thing_the_call_does(void)
{
    // A device that is all zeroes has no usable sockets, so reaching the lock
    // at all would be observable as something other than a clean return. Two
    // further refusals, to pin that the guard does not depend on any field
    // having been initialized.
    katherine_device_t device;
    katherine_acquisition_t acq;
    memset(&device, 0, sizeof(device));
    memset(&acq, 0, sizeof(acq));
    device.acquisition = &acq;

    float first = 0.0f, second = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, 0, &first), KATHERINE_E_STATE);
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, 0, &second), KATHERINE_E_STATE);
}

// A begin that fails must not leave the device claiming a measurement. The
// flag is set on the success path only, after the start command is away, so
// every early return out of katherine_acquisition_begin() -- the argument
// check here, and equally the session lock and the start command further down,
// which this test cannot provoke without a readout -- leaves the device idle.
// Were it set before those, a caller whose begin failed would be refused
// inquiries indefinitely: it has no reason to call read(), and read() is what
// clears the flag.
static void
test_failed_begin_leaves_the_device_idle(void)
{
    katherine_device_t device;
    katherine_acquisition_t acq;
    katherine_tpx3_config_t *const config_p = alloc_config_1();
    KT_REQUIRE(config_p != NULL);

    memset(&device, 0, sizeof(device));
    memset(&acq, 0, sizeof(acq));
    memset(config_p, 0, CONFIG_SIZE_1);

    acq.device = &device;


    // Data-driven readout with more than one frame is rejected before any
    // socket is touched, which is what makes this reachable with a device
    // that has none.
    config_p->common.frame_count = 2;

    // Fast time stamping is left off deliberately. A zeroed config selects
    // whichever frequency is enumerated first, which need not be one where
    // fine time stamping is coherent; asking for it could then trip that guard
    // instead, and this test would pass while exercising something other than
    // the frame-count check it names. Switching it off decouples the two
    // without pinning a frequency here.
    config_p->common.chip_type  = KATHERINE_CHIP_TPX3;
    config_p->common.chip_count = 1;
    config_p->readout_mode      = KATHERINE_TPX3_READOUT_DATA_DRIVEN;
    config_p->px_mode           = KATHERINE_TPX3_PX_TOA_TOT;
    config_p->fast_vco_enable   = false;

    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config_p, true), KATHERINE_E_INVAL);
    KT_CHECK(device.acquisition == NULL);
    free(config_p);
}

// Asking for fine time stamping where it is not coherent is a configuration
// error, refused rather than turned into a stream whose Timestamp = ToA - fToA
// has no meaning. Reachable without a readout for the same reason as the checks
// above: the guard precedes every socket.
//
// The expectation is derived from katherine_tpx3_freq_is_fast_vco_supported rather
// than from a list of frequencies written out here, so this keeps testing that
// the guard agrees with the predicate instead of testing a frozen copy of the
// predicate's current answer.
static void
test_fast_vco_refused_where_incoherent(void)
{
    katherine_device_t device;
    katherine_acquisition_t acq;
    katherine_tpx3_config_t *const config_p = alloc_config_1();
    KT_REQUIRE(config_p != NULL);

    const katherine_tpx3_freq_t all[] = {KATHERINE_TPX3_FREQ_20_MHZ, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_FREQ_80_MHZ, KATHERINE_TPX3_FREQ_160_MHZ};
    unsigned refused                  = 0;

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        // Coherent frequencies are skipped rather than asserted: with the
        // guard passed, begin() would go on to sockets a zeroed device does
        // not have.
        if (katherine_tpx3_freq_is_fast_vco_supported(all[i])) continue;

        memset(&device, 0, sizeof(device));
        memset(&acq, 0, sizeof(acq));
        memset(config_p, 0, CONFIG_SIZE_1);
        acq.device                   = &device;
        config_p->common.frame_count = 1;
        config_p->clock_frequency    = all[i];

        config_p->common.chip_type  = KATHERINE_CHIP_TPX3;
        config_p->common.chip_count = 1;
        config_p->readout_mode      = KATHERINE_TPX3_READOUT_SEQUENTIAL;
        config_p->px_mode           = KATHERINE_TPX3_PX_TOA_TOT;
        config_p->fast_vco_enable   = true;

        KT_CHECK_EQ(katherine_acquisition_begin(&acq, config_p, true), KATHERINE_E_INVAL);
        KT_CHECK(device.acquisition == NULL);
        ++refused;
    }

    free(config_p);

    // A derived loop can pass by running zero times. If every frequency ever
    // becomes coherent, this test has nothing left to say and should fail
    // loudly rather than go green in silence.
    KT_CHECK(refused > 0);
}

// The mirror case -- that slow time stamping is accepted at every frequency --
// is not testable here: with the guard passed, begin() goes on to the sockets
// a zeroed device does not have. The predicate itself is covered by
// test_clock_phases.c.

// A transport that answers the lock and the drain and refuses the wire, so
// that the abort below fails its command without a socket, a bus or a
// readout. Only the operations abort reaches are filled in; a stub for the
// rest would claim coverage this case does not have. The drain is one of
// them because every call that takes the control lock now empties it first.
static katherine_error_t
stub_lock(void *ctx)
{
    (void) ctx;
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_send_cmd(void *ctx, const void *data, size_t count)
{
    (void) ctx;
    (void) data;
    (void) count;
    return KATHERINE_E_CLOSED;
}

static katherine_error_t
stub_flush(void *ctx, uint32_t *discarded)
{
    (void) ctx;
    (void) discarded;
    return KATHERINE_E_OK;
}

static const katherine_transport_ops_t stub_gone_ops = {
    .send_cmd       = stub_send_cmd,
    .flush_incoming = stub_flush,
    .mutex_lock     = stub_lock,
    .mutex_unlock   = stub_lock,
};

// Aborting a readout that has gone away. The command cannot land and the call
// says so, but the flag it exists to raise is the read loop's only way out of
// a stream that will never produce anything again, so it goes up anyway.
// Raising it only after a command that landed left an unreachable readout
// unabortable: on USB the read loop then spins, since nothing there paces a
// failing acquire.
static void
test_aborting_an_unreachable_readout_still_ends_the_run(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));
    device.control_transport.ops  = &stub_gone_ops;
    device.control_transport.kind = KATHERINE_TRANSPORT_USB;

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    acq.device       = &device;
    acq.readout_mode = KATHERINE_TPX3_READOUT_DATA_DRIVEN;

    KT_CHECK_EQ(katherine_acquisition_abort(&acq), KATHERINE_E_CLOSED);
    KT_CHECK(katherine_atomic_load_bool(&acq.aborted));
}


// A data link that has gone away: every attempt to borrow a buffer fails at
// once, as a USB ring does when the readout is unplugged. The count is the
// point of the case -- a loop that retried such a link would call this
// hundreds of thousands of times in a tenth of a second.
static int g_gone_calls;

static katherine_error_t
stub_acquire_gone(void *ctx, void **buf, size_t *len)
{
    (void) ctx;
    (void) buf;
    (void) len;

    ++g_gone_calls;
    return KATHERINE_E_CLOSED;
}

static katherine_error_t
stub_release(void *ctx, void *buf)
{
    (void) ctx;
    (void) buf;
    return KATHERINE_E_OK;
}

static const katherine_transport_ops_t stub_gone_data_ops = {
    .acquire_data_buf = stub_acquire_gone,
    .release_data_buf = stub_release,
    .mutex_lock       = stub_lock,
    .mutex_unlock     = stub_lock,
};

// A link that is gone is not a stream that is quiet: retrying it cannot mend
// it, and on USB the attempt returns at once, so the read loop used to spin
// at full speed for as long as fail_timeout allowed -- for ever, where that
// was left at 0. One attempt is enough to know.
static void
test_a_failed_link_ends_the_read_instead_of_being_retried(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));
    device.derived_info.supported = true;
    device.derived_info.gen       = 2;

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv(&acq);

    acq.device                          = &device;
    acq.state                           = KATHERINE_ACQUISITION_STATE_RUNNING;
    acq.readout_mode                    = KATHERINE_TPX3_READOUT_DATA_DRIVEN;
    acq.px_mode                         = KATHERINE_TPX3_PX_TOA_TOT;
    priv->toa_coarse_tick_to_fine_shift = 4;
    /* Bounded, so the behaviour this replaces ends the test rather than
       hanging it. In the field this was commonly 0, where the old loop
       had no exit at all. */
    acq.fail_timeout        = 100;
    acq.data_transport.ops  = &stub_gone_data_ops;
    acq.data_transport.kind = KATHERINE_TRANSPORT_USB;

    g_gone_calls = 0;
    KT_CHECK_EQ(katherine_acquisition_read(&acq), KATHERINE_E_CLOSED);
    KT_CHECK_EQ(g_gone_calls, 1);
}


// Finalising one acquisition must not disown another. The device holds a
// single registration and the guard above reads exactly it, so clearing it on
// behalf of an acquisition that does not own it lets inquiries through in the
// middle of someone else's measurement. Reachable without a readout: a zeroed
// acquisition owns no transport and no buffers, so fini() frees nothing.
static void
test_fini_leaves_another_acquisitions_registration_alone(void)
{
    katherine_device_t device;
    katherine_acquisition_t running, other;

    memset(&device, 0, sizeof(device));
    memset(&running, 0, sizeof(running));
    memset(&other, 0, sizeof(other));

    running.device     = &device;
    other.device       = &device;
    device.acquisition = &running;

    katherine_acquisition_fini(&other);
    KT_CHECK(device.acquisition == &running);

    // And the guard is still closed, which is the reason the pointer matters.
    float temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, 0, &temperature), KATHERINE_E_STATE);

    // Its owner still clears it.
    katherine_acquisition_fini(&running);
    KT_CHECK(device.acquisition == NULL);
}

// The device holds exactly one acquisition pointer, and that pointer is what
// katherine_device_fini() refuses to tear down under. Asserting the
// invariant rather than the refusal: the refusal behaves differently by
// build -- an assertion where they are live, a leaked link where they are
// not -- while the invariant is the thing a regression would break, and it
// holds either way.
static void
test_a_device_knows_which_acquisition_is_live(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    acq.device = &device;

    // Registered the way katherine_acquisition_begin() leaves it.
    device.acquisition = &acq;
    KT_CHECK(device.acquisition == &acq);

    // And the guard that reads it is closed while it is held.
    float temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&device, 0, &temperature), KATHERINE_E_STATE);

    // Finalising the acquisition is what gives the device back: only then is
    // katherine_device_fini() safe, which is the order the headers state.
    katherine_acquisition_fini(&acq);
    KT_CHECK(device.acquisition == NULL);
}


int
main(void)
{
    KT_RUN(test_sensor_temperature_refused_while_running);
    KT_RUN(test_guard_is_the_first_thing_the_call_does);
    KT_RUN(test_failed_begin_leaves_the_device_idle);
    KT_RUN(test_fast_vco_refused_where_incoherent);
    KT_RUN(test_aborting_an_unreachable_readout_still_ends_the_run);
    KT_RUN(test_a_failed_link_ends_the_read_instead_of_being_retried);
    KT_RUN(test_a_device_knows_which_acquisition_is_live);

    KT_RUN(test_fini_leaves_another_acquisitions_registration_alone);
    return kt_summary();
}
