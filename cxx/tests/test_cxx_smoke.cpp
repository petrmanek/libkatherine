/**
 * \file
 * \brief Every katherinexx type: declared, constructible, callable, no crash.
 *
 * The C++ layer had no test of any kind. Everything under ctest was C, so a
 * green suite said nothing about whether a wrapper worked: a mistranscribed
 * field, a typo'd accessor or an enumerator wired to the wrong C value
 * compiles, ships, and is found by whoever uses it first.
 *
 * Deliberately weaker than the C suite, and the bar is worth stating because
 * it decides what is missing here on purpose. The underlying API's
 * correctness is tested in c/tests and is not retested through a wrapper;
 * numbers are not restated. What is asserted is that every type this library
 * declares can be constructed, that every accessor can be called, that a
 * value written through a setter comes back from its getter, and that a
 * failure surfaces as the documented exception rather than as a crash.
 *
 * No hardware, no peer, and no global resource: the one socket opened here
 * takes an ephemeral local port. katherine::device and katherine::acquisition
 * are therefore absent, katherine_device_init() binding the fixed ports
 * 1555/1556 -- they are covered by test_cxx_device.cpp, which has to be
 * serialized against the emulator tests for that reason.
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

// Renders v through its operator<< and returns what came out. Every repr.hpp
// overload is exercised through this, so the assertion is uniform: a
// rendering that is empty means the overload resolved to something that wrote
// nothing, and one that throws means it resolved to something worse.
template<typename T>
static std::string
streamed(const T& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

// ------------------------------------------------------------------
// 1. version.hpp

static void
test_version(void)
{
    KT_CHECK(katherine::version() > 0);
    KT_CHECK(katherine::version_string() != nullptr);
    KT_CHECK(std::string(katherine::version_string()).size() > 0);
}

// ------------------------------------------------------------------
// 2. The enumerations, and the operator<< overloads added for them.
//
// Streamed rather than compared against the C strings, which c/tests/
// test_repr.c already pins. What this catches is an overload that does not
// resolve -- which for these is the live question, the four tpx3:: ones
// needing argument-dependent lookup to find an operator<< in a nested
// namespace.

static void
test_enum_rendering(void)
{
    KT_CHECK(streamed(katherine::polarity::holes).size() > 0);
    KT_CHECK(streamed(katherine::polarity::electrons).size() > 0);
    KT_CHECK(streamed(katherine::acq_state::running).size() > 0);
    KT_CHECK(streamed(katherine::phase_correction::hardware).size() > 0);

    KT_CHECK(streamed(katherine::tpx3::phase::p1).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::freq::f40).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::px_mode::toa_tot).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::readout_mode::sequential).size() > 0);

    // Two members of one enumeration must not render alike, which is what a
    // stringifier wired to the wrong enumeration would produce.
    KT_CHECK(streamed(katherine::polarity::holes) != streamed(katherine::polarity::electrons));
    KT_CHECK(streamed(katherine::tpx3::freq::f40) != streamed(katherine::tpx3::freq::f80));
}

// ------------------------------------------------------------------
// 3. katherine::config, every accessor round-tripped.
//
// The one place in this file where values are asserted, because the
// assertion is about the wrapper rather than about the library: a setter and
// getter pair wired to different struct members is exactly the defect this
// issue was filed for, and it is invisible to every C test.

static void
test_config_round_trip(void)
{
    katherine::config c;

    c.set_bias_id(7);
    KT_CHECK_EQ(c.bias_id(), 7);

    c.set_no_frames(12);
    KT_CHECK_EQ(c.no_frames(), 12);

    c.set_bias(150.0f);
    KT_CHECK_CLOSE(c.bias(), 150.0f);

    c.set_delayed_start(true);
    KT_CHECK(c.delayed_start());
    c.set_delayed_start(false);
    KT_CHECK(!c.delayed_start());

    c.set_gray_disable(true);
    KT_CHECK(c.gray_disable());
    c.set_gray_disable(false);
    KT_CHECK(!c.gray_disable());

    c.set_correct_phase(true);
    KT_CHECK(c.correct_phase());
    c.set_correct_phase(false);
    KT_CHECK(!c.correct_phase());

    // Hole collection is the safe value and the one a zeroed configuration
    // takes; both directions are written and read back here so that the
    // accessor pair cannot silently invert, which on this field destroys the
    // chip (see katherine_polarity_t).
    c.set_polarity(katherine::polarity::holes);
    KT_CHECK(c.polarity() == katherine::polarity::holes);
    c.set_polarity(katherine::polarity::electrons);
    KT_CHECK(c.polarity() == katherine::polarity::electrons);

    c.set_phase(katherine::tpx3::phase::p4);
    KT_CHECK(c.phase() == katherine::tpx3::phase::p4);

    c.set_freq(katherine::tpx3::freq::f80);
    KT_CHECK(c.freq() == katherine::tpx3::freq::f80);

    c.set_acq_time(std::chrono::milliseconds{250});
    KT_CHECK(c.acq_time() == std::chrono::milliseconds{250});

    katherine::trigger start{};
    start.enabled = true;
    start.channel = 3;
    c.set_start_trigger(start);
    KT_CHECK(c.start_trigger().enabled);
    KT_CHECK_EQ(c.start_trigger().channel, 3);

    katherine::trigger stop{};
    stop.enabled = false;
    c.set_stop_trigger(stop);
    KT_CHECK(!c.stop_trigger().enabled);

    katherine::test_pulse_config tp{};
    tp.enabled = true;
    tp.count   = 5;
    c.set_test_pulse_config(tp);
    KT_CHECK(c.test_pulse_config().enabled);
    KT_CHECK_EQ(c.test_pulse_config().count, 5);

    katherine::dacs d{};
    d.named.Vthreshold_fine = 321;
    c.set_dacs(d);
    KT_CHECK_EQ(c.dacs().named.Vthreshold_fine, 321);

    // The non-const accessors hand out references into the owned struct, so a
    // write through one must be visible to the const one.
    c.dacs().named.Vthreshold_coarse = 8;
    KT_CHECK_EQ(c.dacs().named.Vthreshold_coarse, 8);
}

// ------------------------------------------------------------------
// 4. katherine::px_config, and the four loaders.

static void
test_px_config(void)
{
    katherine::px_config px{};
    const katherine::coord c = {12, 34};

    px.set_mask_bit(c, true);
    KT_CHECK(px.mask_bit(c));
    px.set_mask_bit(c, false);
    KT_CHECK(!px.mask_bit(c));

    px.set_test_bit(c, true);
    KT_CHECK(px.test_bit(c));
    px.set_test_bit(c, false);
    KT_CHECK(!px.test_bit(c));

    // The full 4-bit range, this being the field whose nibble order was
    // reversed until recently (see katherine_px_config_set_loc_thl()).
    for (std::uint8_t thl = 0; thl < 16; ++thl) {
        px.set_loc_thl(c, thl);
        KT_CHECK_EQ(px.loc_thl(c), thl);
    }

    // The data loaders, over correctly sized buffers of zeros. What is
    // asserted is that they return a configuration rather than throwing, the
    // contents being the C suite's business.
    katherine::bmc bmc{};
    katherine::bpc bpc{};

    bool bmc_ok = true;
    try {
        katherine::px_config from_bmc = katherine::load_bmc_data(bmc);
        KT_CHECK(streamed(from_bmc).size() > 0);
    } catch (const katherine::error&) {
        bmc_ok = false;
    }
    KT_CHECK(bmc_ok);

    bool bpc_ok = true;
    try {
        katherine::px_config from_bpc = katherine::load_bpc_data(bpc);
        KT_CHECK(streamed(from_bpc).size() > 0);
    } catch (const katherine::error&) {
        bpc_ok = false;
    }
    KT_CHECK(bpc_ok);

    // The file loaders, on a path that does not exist. The exception is the
    // point: these are the only katherinexx functions whose failure a caller
    // can provoke without a device.
    bool bmc_file_threw = false;
    try {
        (void) katherine::load_bmc_file("/nonexistent/katherine-smoke.bmc");
    } catch (const katherine::error&) {
        bmc_file_threw = true;
    }
    KT_CHECK(bmc_file_threw);

    bool bpc_file_threw = false;
    try {
        (void) katherine::load_bpc_file("/nonexistent/katherine-smoke.bpc");
    } catch (const katherine::error&) {
        bpc_file_threw = true;
    }
    KT_CHECK(bpc_file_threw);
}

// ------------------------------------------------------------------
// 5. toa.hpp -- the #31 timestamp surface, which shipped with no C++ test.

static void
test_timestamps(void)
{
    const katherine::tpx3::freq freqs[] = {
        katherine::tpx3::freq::f20,
        katherine::tpx3::freq::f40,
        katherine::tpx3::freq::f80,
        katherine::tpx3::freq::f160,
    };

    for (std::size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); ++i) {
        const std::uint8_t ticks = katherine::tpx3::toa_coarse_tick_to_fine_ticks(freqs[i]);
        const std::uint8_t shift = katherine::tpx3::toa_coarse_tick_to_fine_shift(freqs[i]);

        KT_CHECK(ticks > 0);

        // A relation between two functions rather than a number from the C
        // suite: the shift is the tick count's exponent, so a swapped or
        // mistranscribed pair fails without this test knowing either value.
        //
        // Shifted as 64-bit because KT_CHECK_EQ compares through unsigned
        // long long: a 32-bit shift widened after the fact is what MSVC warns
        // about as C4334, and rightly, the widening being the interesting
        // part of such a bug when the shift is a variable.
        KT_CHECK_EQ(std::uint64_t{1} << shift, ticks);

        KT_CHECK(katherine::tpx3::toa_epoch_bias(shift) > 0);
    }

    const std::uint8_t shift = katherine::tpx3::toa_coarse_tick_to_fine_shift(katherine::tpx3::freq::f40);

    const katherine::tpx3::timestamp_seconds s = katherine::tpx3::timestamp_to_seconds(std::uint64_t{1} << 40);
    KT_CHECK(s.sec > 0);
    KT_CHECK(s.nsec >= 0.0);

    const katherine::tpx3::toa_ftoa t = katherine::tpx3::timestamp_to_toa_ftoa(shift, 0, std::uint64_t{1} << 40);
    KT_CHECK(t.toa > 0);
}

// ------------------------------------------------------------------
// 6. The exception hierarchy.

static void
test_errors(void)
{
    // katherine::error is a std::runtime_error, which is what lets a caller
    // who does not know this library still catch it.
    bool caught_as_runtime_error = false;
    try {
        throw katherine::error{"smoke"};
    } catch (const std::runtime_error& e) {
        caught_as_runtime_error = (std::string(e.what()) == "smoke");
    }
    KT_CHECK(caught_as_runtime_error);

    // katherine::system_error carries a library error code and renders it
    // through katherine_strerror(), so its message is the code's own.
    bool caught_as_katherine_error = false;
    std::string what;
    try {
        throw katherine::system_error{KATHERINE_E_TIMEOUT};
    } catch (const katherine::error& e) {
        caught_as_katherine_error = true;
        what                      = e.what();
    }
    KT_CHECK(caught_as_katherine_error);
    KT_CHECK(what.size() > 0);
    KT_CHECK(what == std::string(katherine_strerror(KATHERINE_E_TIMEOUT)));
}

// ------------------------------------------------------------------
// 7. katherine::udp, on a socket with no peer.

static void
test_udp(void)
{
    katherine::udp u{"127.0.0.1", 0, "127.0.0.1", 1555, 50};

    KT_CHECK(streamed(u).size() > 0);
    KT_CHECK(u.c_udp() != nullptr);

    // Callable, and none of these needs a peer.
    u.set_strict_ack(true);
    u.set_strict_ack(false);
    u.set_remote("127.0.0.1", 1556);
    u.pin_remote();

    // An empty socket yields KATHERINE_E_TIMEOUT, which the wrapper raises as
    // katherine::system_error. Asserted by type, and by the code it carries.
    bool threw_timeout = false;
    try {
        unsigned char buf[16];
        std::size_t count = sizeof(buf);
        u.recv(buf, count);
    } catch (const katherine::error& e) {
        threw_timeout = (std::string(e.what()) == std::string(katherine_strerror(KATHERINE_E_TIMEOUT)));
    }
    KT_CHECK(threw_timeout);

    // A malformed address is rejected before any socket work.
    bool threw_addr = false;
    try {
        katherine::udp bad{"not-an-address", 0, "127.0.0.1", 1555, 50};
        (void) bad;
    } catch (const katherine::error&) {
        threw_addr = true;
    }
    KT_CHECK(threw_addr);
}

// ------------------------------------------------------------------
// 8. repr.hpp over every remaining type it claims to render.

static void
test_struct_rendering(void)
{
    KT_CHECK(streamed(katherine::coord{1, 2}).size() > 0);

    KT_CHECK(streamed(katherine_px_toa_tot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_px_f_toa_tot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_px_toa_only_t{}).size() > 0);
    KT_CHECK(streamed(katherine_px_f_toa_only_t{}).size() > 0);
    KT_CHECK(streamed(katherine_px_event_count_itot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_px_f_event_count_itot_t{}).size() > 0);

    KT_CHECK(streamed(katherine::trigger{}).size() > 0);
    KT_CHECK(streamed(katherine::test_pulse_config{}).size() > 0);
    KT_CHECK(streamed(katherine::dacs{}).size() > 0);
    KT_CHECK(streamed(katherine::frame_info{}).size() > 0);
    KT_CHECK(streamed(katherine_frame_info_time_t{}).size() > 0);
    KT_CHECK(streamed(katherine_readout_status_t{}).size() > 0);
    KT_CHECK(streamed(katherine_comm_status_t{}).size() > 0);

    // The wrapper classes, rendered over their accessors rather than as the
    // C struct they own.
    katherine::config c;
    KT_CHECK(streamed(c).size() > 0);

    // A px_config renders as a digest, so it is short however large it is.
    katherine::px_config px{};
    KT_CHECK(streamed(px).size() > 0);
}

int
main(void)
{
    KT_RUN(test_version);
    KT_RUN(test_enum_rendering);
    KT_RUN(test_config_round_trip);
    KT_RUN(test_px_config);
    KT_RUN(test_timestamps);
    KT_RUN(test_errors);
    KT_RUN(test_udp);
    KT_RUN(test_struct_rendering);
    return kt_summary();
}
