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
 * Still no hardware and no peer. A device opens against an address nothing
 * answers at, asking for the sockets alone, and its methods then fail, which
 * is how the error paths are reached. Without KATHERINE_DEVICE_DONT_ENUMERATE
 * the open itself would fail, enumeration being part of it. The bar is the
 * same as in the smoke test: constructible, callable, and failing as a
 * katherine::error rather than as a crash, a hang or a C return code leaking
 * through. Which error is not asserted, since that depends on how the
 * platform treats an unassigned loopback address; see DEAD_ADDRESS below, whose
 * choice is less obvious than it looks.
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

// A loopback address with nothing on it, and deliberately not 127.0.0.2:
// macOS configures only 127.0.0.1 by default, so CI aliases that one for the
// emulator to live on (see the workflow), which makes it a live local address
// there. Pointing a device at a live address is worse than useless -- the
// device's own control socket is bound to the same port on every interface,
// so a command sent to a local address arrives back at the socket that sent
// it, and the ack correlation accepts it, since a request carries its opcode
// in the very byte a response is matched on. The call then "succeeds" against
// nothing. Confirmed on Linux with 127.0.0.1, and it is what this test failed
// on under macOS.
//
// What makes an address safe here is that no local interface is ASSIGNED it,
// which is narrower than nothing listening on it and narrower than it not
// being routed. Linux routes all of 127/8 to loopback, yet a datagram to
// 127.0.0.3 is still not delivered to a socket bound to 0.0.0.0, because
// delivery needs the destination to be an assigned address -- which is why
// 127.0.0.1 self-echoes and 127.0.0.3 does not. macOS assigns only
// 127.0.0.1, plus whatever CI aliases.
//
// Whichever way the send then fails -- an expired receive timeout, or an
// immediate error on a platform that rejects the destination outright -- a
// katherine::error is raised, which is all this file asserts. No datagram
// leaves the host either way.
#define DEAD_ADDRESS "udp://127.0.0.3"

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
    // Sockets only: nothing answers at this address, and what is under test
    // is the methods below rather than the open.
    katherine::device dev{DEAD_ADDRESS, KATHERINE_DEVICE_DONT_ENUMERATE};

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

    // Two calls C++ could not reach until they were bound. verify_config()
    // refuses before the socket on a device nobody told what it is, which is
    // a different failure from the timeouts above and worth separating:
    // KATHERINE_E_STATE rather than KATHERINE_E_TIMEOUT.
    katherine_error_t code = KATHERINE_E_OK;
    try {
        (void) dev.verify_config();
    } catch (const katherine::system_error& e) {
        code = e.code();
    }
    KT_CHECK_EQ(code, KATHERINE_E_STATE);

    threw = false;
    try {
        dev.set_token_count(8);
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    // declare() takes the caller's word where enumerate() would have to ask,
    // and is the only way to reach the generation-dependent calls on a
    // device opened with DONT_ENUMERATE. Nothing is on the wire for it.
    KT_CHECK(!dev.derived_info().supported);
    KT_CHECK(streamed(dev.info()).size() > 0);

    katherine_device_info_t declared = {};
    declared.hw_type                 = 0x03;
    declared.chip_count              = 4;
    dev.declare(declared);

    KT_CHECK(dev.derived_info().supported);
    KT_CHECK_EQ(dev.derived_info().gen, 2);
    KT_CHECK_EQ(dev.derived_info().chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK_EQ(dev.info().chip_count, 4);
    KT_CHECK(streamed(dev.derived_info()).size() > 0);

    // And now the same call that was KATHERINE_E_STATE above reaches the
    // wire, where it fails on the timeout like every other one.
    code = KATHERINE_E_OK;
    try {
        (void) dev.verify_config();
    } catch (const katherine::system_error& e) {
        code = e.code();
    }
    KT_CHECK_EQ(code, KATHERINE_E_TIMEOUT);

    // Both of the calls the constructor makes unless told not to.
    threw = false;
    try {
        dev.enumerate();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(threw);

    // select_interface() is the one control call that succeeds without a
    // peer: the readout never acknowledges it, so nothing waits for a reply
    // and a send into the void is a send.
    threw = false;
    try {
        dev.select_interface();
    } catch (const katherine::error&) {
        threw = true;
    }
    KT_CHECK(!threw);

    // An acquisition constructs -- it allocates its buffers and takes its
    // timeouts, touching no socket -- and reports the state of one that has
    // not begun.
    katherine::acquisition<katherine::acq::f_toa_tot> acq{
        dev, katherine::md_size * 128, sizeof(katherine_tpx3_px_f_toa_tot_t) * 128, std::chrono::milliseconds{10},
        std::chrono::milliseconds{50}, true};

    KT_CHECK(acq.state() == katherine::acq_state::not_started);
    KT_CHECK_EQ(acq.completed_frames(), 0);
    KT_CHECK_EQ(acq.requested_frames(), 0);
    KT_CHECK(!acq.aborted());
    KT_CHECK(!acq.dropped_measurement_data());
    KT_CHECK(acq.c_acq() != nullptr);

    // Streamed like config, device and udp, which it could not be until
    // base_acquisition gained c_acq(). Resolved on the base, so this one
    // overload covers every acquisition mode.
    KT_CHECK(streamed(acq).size() > 0);

    // The handler setters take ownership of a callable; none is invoked here,
    // since nothing will arrive.
    acq.set_pixels_received_handler([](const katherine_tpx3_px_f_toa_tot_t *, std::size_t) { });
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
