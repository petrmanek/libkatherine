/**
 * \file
 * \brief katherine::device and katherine::acquisition, against nothing.
 *
 * Split out of test_cxx_smoke.cpp because these two types cannot be touched
 * without claiming a host-global resource: katherine_device_init() binds the
 * fixed ports 1555/1556, the same ones the emulator tests use, so this runs
 * serialized and under the e2e label while the rest of the C++ surface stays
 * a unit test.
 *
 * Still no hardware and no peer. A device constructs against an address
 * nothing answers at -- UDP being connectionless, that succeeds -- and its
 * methods then fail on the receive timeout, which is how the error paths are
 * reached. The bar is the same as in the smoke test: constructible, callable,
 * and failing as a katherine::error rather than as a crash, a hang or a C
 * return code leaking through.
 *
 * \author Petr Mánek
 * \date 15.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>

#include <katherinexx/katherinexx.hpp>

#include "ktest.h"

// An address in the loopback network that nothing is bound to. Reaching it
// costs one receive timeout per call, measured at about 100 ms, which is what
// makes exercising the failure paths affordable.
#define DEAD_ADDR "127.0.0.2"

// Renders v through its operator<< and returns what came out.
template<typename T>
static std::string
streamed(const T& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

// ------------------------------------------------------------------
// katherine::device and katherine::acquisition.

static void
test_device_and_acquisition(void)
{
    katherine::device dev{DEAD_ADDR};

    KT_CHECK(dev.c_dev() != nullptr);
    KT_CHECK(streamed(dev).size() > 0);

    // Reports false without asking the readout, so it costs no timeout.
    KT_CHECK(!dev.can_correct_timestamp_phase());

    // Every control call has to reach the readout, so each fails on the
    // receive timeout. That the failure is a katherine::error rather than a
    // crash, a hang or a C return code leaking through is the whole claim.
    bool threw = false;
    try {
        (void) dev.chip_id();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    threw = false;
    try {
        (void) dev.readout_status();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    threw = false;
    try {
        (void) dev.comm_status();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    threw = false;
    try {
        (void) dev.readout_temperature();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    // An acquisition constructs -- it allocates its buffers and takes its
    // timeouts, touching no socket -- and reports the state of one that has
    // not begun.
    katherine::acquisition<katherine::acq::f_toa_tot> acq{
        dev, katherine::md_size * 128, sizeof(katherine_px_f_toa_tot_t) * 128, std::chrono::milliseconds{10},
        std::chrono::milliseconds{50}, true};

    KT_CHECK(acq.state() == katherine::acq_state::not_started);
    KT_CHECK_EQ(acq.completed_frames(), 0);
    KT_CHECK_EQ(acq.requested_frames(), 0);
    KT_CHECK(!acq.aborted());
    KT_CHECK(!acq.dropped_measurement_data());

    // Not streamed, unlike config, device and udp: base_acquisition keeps its
    // katherine_acquisition_t private and hands out no accessor, so the
    // operator<< repr.hpp carries for that struct cannot be reached through
    // the wrapper at all.

    // The handler setters take ownership of a callable; none is invoked here,
    // since nothing will arrive.
    acq.set_pixels_received_handler([](const katherine_px_f_toa_tot_t *, std::size_t) { });
    acq.set_frame_started_handler([](int) { });
    acq.set_frame_ended_handler([](int, bool, const katherine::frame_info&) { });
    acq.set_data_received_handler([](const char *, std::size_t) { });

    // begin() configures the device, so it fails the same way its calls do.
    katherine::config c;
    c.set_bias_id(0);
    c.set_no_frames(1);
    c.set_acq_time(std::chrono::milliseconds{1});

    threw = false;
    try {
        acq.begin(c, katherine::tpx3::readout_mode::data_driven);
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);
}

int
main(void)
{
    KT_RUN(test_device_and_acquisition);
    return kt_summary();
}
