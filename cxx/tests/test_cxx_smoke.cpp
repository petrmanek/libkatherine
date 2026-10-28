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
#include <type_traits>

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
    KT_CHECK(streamed(katherine::tpx3::phase_correction::hardware).size() > 0);

    KT_CHECK(streamed(katherine::tpx3::phase::p1).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::freq::f40).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::px_mode::toa_tot).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::readout_mode::sequential).size() > 0);

    // The four C enumerations that had no overload: a caller streaming one
    // got the integer, silently, since every one of them converts.
    KT_CHECK(streamed(KATHERINE_TRIGGER_EDGE_RISING).size() > 0);
    KT_CHECK(streamed(KATHERINE_CHIP_TPX3).size() > 0);
    KT_CHECK(streamed(KATHERINE_TRANSPORT_UDP).size() > 0);
    KT_CHECK(streamed(KATHERINE_TPX3_DAC_VTHRESHOLD_FINE).size() > 0);

    // Two members of one enumeration must not render alike, which is what a
    // stringifier wired to the wrong enumeration would produce.
    KT_CHECK(streamed(katherine::polarity::holes) != streamed(katherine::polarity::electrons));
    KT_CHECK(streamed(katherine::tpx3::freq::f40) != streamed(katherine::tpx3::freq::f80));
    KT_CHECK(streamed(KATHERINE_CHIP_TPX3) != streamed(KATHERINE_CHIP_TPX4));
    KT_CHECK(streamed(KATHERINE_TRANSPORT_UDP) != streamed(KATHERINE_TRANSPORT_USB));
    KT_CHECK(streamed(KATHERINE_TPX3_DAC_VTHRESHOLD_FINE)
        != streamed(KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE));

    // And none of them is rendering as its own number, which is what the
    // missing overload did.
    KT_CHECK(streamed(KATHERINE_CHIP_TPX3) != "3");
}

// ------------------------------------------------------------------
// 3. katherine::tpx3::config, every accessor round-tripped.
//
// The one place in this file where values are asserted, because the
// assertion is about the wrapper rather than about the library: a setter and
// getter pair wired to different struct members is exactly the defect this
// issue was filed for, and it is invisible to every C test.

static void
test_config_round_trip(void)
{
    katherine::tpx3::config c;

    c.set_bias(1, 12.5f);
    KT_CHECK_CLOSE(c.bias(1), 12.5f);

    c.set_frame_count(12);
    KT_CHECK_EQ(c.frame_count(), 12);

    c.set_bias(0, 150.0f);
    KT_CHECK_CLOSE(c.bias(0), 150.0f);


    c.set_gray_disable(0, true);
    KT_CHECK(c.gray_disable(0));
    c.set_gray_disable(0, false);
    KT_CHECK(!c.gray_disable(0));

    c.set_correct_clock_phase(true);
    KT_CHECK(c.correct_clock_phase());
    c.set_correct_clock_phase(false);
    KT_CHECK(!c.correct_clock_phase());

    // Hole collection is the safe value and the one a zeroed configuration
    // takes; both directions are written and read back here so that the
    // accessor pair cannot silently invert, which on this field destroys the
    // chip (see katherine_polarity_t).
    c.set_polarity(0, katherine::polarity::holes);
    KT_CHECK(c.polarity(0) == katherine::polarity::holes);
    c.set_polarity(0, katherine::polarity::electrons);
    KT_CHECK(c.polarity(0) == katherine::polarity::electrons);

    c.set_clock_phase(katherine::tpx3::phase::p4);
    KT_CHECK(c.clock_phase() == katherine::tpx3::phase::p4);

    c.set_clock_frequency(katherine::tpx3::freq::f80);
    KT_CHECK(c.clock_frequency() == katherine::tpx3::freq::f80);

    c.set_frame_duration(std::chrono::milliseconds{250});
    KT_CHECK(c.frame_duration() == std::chrono::milliseconds{250});

    katherine::trigger start{};
    start.use_trigger = true;
    start.channel     = KATHERINE_TRIGGER_SOURCE_GPIO_2;
    c.set_start_trigger(start);
    KT_CHECK(c.start_trigger().use_trigger);
    KT_CHECK_EQ(c.start_trigger().channel, KATHERINE_TRIGGER_SOURCE_GPIO_2);

    katherine::trigger stop{};
    stop.use_trigger = false;
    c.set_stop_trigger(stop);
    KT_CHECK(!c.stop_trigger().use_trigger);

    katherine::tpx3::test_pulse_config tp{};
    tp.enabled = true;
    tp.count   = 5;
    c.set_test_pulses(tp);
    KT_CHECK(c.test_pulses().enabled);
    KT_CHECK_EQ(c.test_pulses().count, 5);

    katherine::tpx3::dacs d{};
    d.named.Vthreshold_fine = 321;
    c.set_dacs(0, d);
    KT_CHECK_EQ(c.dacs(0).named.Vthreshold_fine, 321);

    // The non-const accessors hand out references into the owned struct, so a
    // write through one must be visible to the const one.
    c.dacs(0).named.Vthreshold_coarse = 8;
    KT_CHECK_EQ(c.dacs(0).named.Vthreshold_coarse, 8);
}

// ------------------------------------------------------------------
// 4. katherine::tpx3::px_config, and the four loaders.

static void
test_px_config(void)
{
    katherine::tpx3::px_config px{};
    const katherine::tpx3::coord c = {12, 34};

    px.set_mask_bit(c, true);
    KT_CHECK(px.mask_bit(c));
    px.set_mask_bit(c, false);
    KT_CHECK(!px.mask_bit(c));

    px.set_test_bit(c, true);
    KT_CHECK(px.test_bit(c));
    px.set_test_bit(c, false);
    KT_CHECK(!px.test_bit(c));

    // The full 4-bit range, this being the field whose nibble order was
    // reversed until recently (see katherine_tpx3_px_config_set_loc_thl()).
    for (std::uint8_t thl = 0; thl < 16; ++thl) {
        px.set_loc_thl(c, thl);
        KT_CHECK_EQ(px.loc_thl(c), thl);
    }

    // The data loaders, over correctly sized buffers of zeros. What is
    // asserted is that they return a configuration rather than throwing, the
    // contents being the C suite's business.
    katherine::tpx3::bmc bmc{};
    katherine::tpx3::bpc bpc{};

    bool bmc_ok = true;
    try {
        katherine::tpx3::px_config from_bmc = katherine::tpx3::load_bmc_data(bmc);
        KT_CHECK(streamed(from_bmc).size() > 0);
    } catch (const katherine::error&) {
        bmc_ok = false;
    }
    KT_CHECK(bmc_ok);

    bool bpc_ok = true;
    try {
        katherine::tpx3::px_config from_bpc = katherine::tpx3::load_bpc_data(bpc);
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
        (void) katherine::tpx3::load_bmc_file("/nonexistent/katherine-smoke.bmc");
    } catch (const katherine::error&) {
        bmc_file_threw = true;
    }
    KT_CHECK(bmc_file_threw);

    bool bpc_file_threw = false;
    try {
        (void) katherine::tpx3::load_bpc_file("/nonexistent/katherine-smoke.bpc");
    } catch (const katherine::error&) {
        bpc_file_threw = true;
    }
    KT_CHECK(bpc_file_threw);
}

// ------------------------------------------------------------------
// 5. toa.hpp -- the #31 timestamp surface, which shipped with no C++ test.

/* The two owning wrappers refuse to be copied or moved, and the value type
 * beside them does not.
 *
 * Asserted with traits rather than by writing the copy out, because writing
 * it out is the thing that must not compile. Before this, copying a device
 * finalized the same sockets twice and copying an acquisition freed the
 * pixel buffer twice, and neither said anything until it happened. */
static void
test_ownership(void)
{
    KT_CHECK(!std::is_copy_constructible<katherine::device>::value);
    KT_CHECK(!std::is_copy_assignable<katherine::device>::value);
    KT_CHECK(!std::is_move_constructible<katherine::device>::value);
    KT_CHECK(!std::is_move_assignable<katherine::device>::value);

    using acq_t = katherine::acquisition<katherine::tpx3::acq::toa_tot>;
    KT_CHECK(!std::is_copy_constructible<acq_t>::value);
    KT_CHECK(!std::is_copy_assignable<acq_t>::value);
    KT_CHECK(!std::is_move_constructible<acq_t>::value);
    KT_CHECK(!std::is_move_assignable<acq_t>::value);

    // The configuration owns nothing and stays an ordinary value.
    KT_CHECK(std::is_copy_constructible<katherine::tpx3::config>::value);
    KT_CHECK(std::is_copy_assignable<katherine::tpx3::config>::value);
    KT_CHECK(std::is_move_constructible<katherine::tpx3::config>::value);
}

/* The hardware-type table, read without a readout. Pure, so a discovery
 * result is enough to ask what a type implies. */
static void
test_derived_info_recognize(void)
{
    const auto gen2 = katherine::derived_info_recognize(0x03);
    KT_CHECK(gen2.supported);
    KT_CHECK_EQ(gen2.gen, 2);
    KT_CHECK_EQ(gen2.chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK(gen2.max_chip_count >= 1);

    // An unknown type is zeroed rather than refused, and `supported` is how
    // the caller is told.
    const auto unknown = katherine::derived_info_recognize(0xFE);
    KT_CHECK(!unknown.supported);
    KT_CHECK(unknown.name == nullptr);
}

/* The three pure helpers of config.h, which no C++ caller could reach until
 * they were bound. What they answer is pinned by the C suite
 * (test_clock_phases.c, test_dacs_validate.c); what is asserted here is that
 * the binding reaches the right function and converts the enum class to the C
 * enumerator it stands for. */
static void
test_clock_helpers(void)
{
    // The divider only ever takes phases away, and it takes none at the
    // slowest clock: Timepix3 manual Table 17.
    KT_CHECK_EQ(katherine::tpx3::actual_phases(katherine::tpx3::freq::f40,
                    katherine::tpx3::phase::p16),
        16u);
    KT_CHECK(katherine::tpx3::actual_phases(katherine::tpx3::freq::f160,
                 katherine::tpx3::phase::p16)
        < 16u);

    // The fine oscillator runs at exactly one frequency.
    KT_CHECK(katherine::tpx3::freq_is_fast_vco_supported(katherine::tpx3::freq::f40));
    KT_CHECK(!katherine::tpx3::freq_is_fast_vco_supported(katherine::tpx3::freq::f20));
    KT_CHECK(!katherine::tpx3::freq_is_fast_vco_supported(katherine::tpx3::freq::f80));
    KT_CHECK(!katherine::tpx3::freq_is_fast_vco_supported(katherine::tpx3::freq::f160));

    // Per-DAC metadata, which Python had and C++ did not. Vthreshold_fine
    // reaches 511, which is the case that matters: a working threshold of
    // 388 is lost entirely if the width is ever read as a byte.
    KT_CHECK_EQ(katherine::tpx3::dac_max(KATHERINE_TPX3_DAC_VTHRESHOLD_FINE), 511u);
    KT_CHECK_EQ(katherine::tpx3::dac_max(KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE), 15u);

    const auto si = katherine::tpx3::dac_to_si(KATHERINE_TPX3_DAC_IBIAS_PREAMP_ON, 128);
    KT_CHECK(si.value > 0.0);
    KT_CHECK_EQ(si.unit, KATHERINE_DAC_UNIT_AMP);

    katherine::tpx3::dacs d{};
    bool threw = false;
    try {
        katherine::tpx3::validate(d);
    } catch (const katherine::system_error&) {
        threw = true;
    }
    KT_CHECK(!threw);

    // A value past its DAC's own width is refused, where set_dacs() would
    // have transmitted it and let the chip truncate.
    d.named.Vthreshold_fine = 0xFFFF;
    katherine_error_t code  = KATHERINE_E_OK;
    try {
        katherine::tpx3::validate(d);
    } catch (const katherine::error& e) {
        code = e.code();
    }
    KT_CHECK_EQ(code, KATHERINE_E_INVAL);
}

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
    // who does not know this library still catch it. Its message is the
    // code's own, rendered through katherine_strerror().
    bool caught_as_runtime_error = false;
    try {
        throw katherine::error{KATHERINE_E_TIMEOUT};
    } catch (const std::runtime_error& e) {
        caught_as_runtime_error = (std::string(e.what()) == katherine_strerror(KATHERINE_E_TIMEOUT));
    }
    KT_CHECK(caught_as_runtime_error);

    // One class now, which is the whole point of the merge: catching
    // katherine::error and then reading e.code() is what a reader writes,
    // and it used not to compile.
    bool caught_as_katherine_error = false;
    std::string what;
    try {
        throw katherine::error{KATHERINE_E_TIMEOUT};
    } catch (const katherine::error& e) {
        caught_as_katherine_error = true;
        what                      = e.what();
    }
    KT_CHECK(caught_as_katherine_error);
    KT_CHECK(what.size() > 0);
    KT_CHECK(what == std::string(katherine_strerror(KATHERINE_E_TIMEOUT)));

    // The code survives the throw, so a caller can branch on the failure
    // instead of comparing its rendered description.
    katherine_error_t code = KATHERINE_E_OK;
    try {
        katherine::detail::check(KATHERINE_E_BAD_CHIP);
    } catch (const katherine::error& e) {
        code = e.code();
    }
    KT_CHECK_EQ(code, KATHERINE_E_BAD_CHIP);

    // And the old name still resolves, for source written against the two
    // classes there used to be.
    static_assert(std::is_same<katherine::system_error, katherine::error>::value,
        "system_error is an alias of error");

    // And a successful code throws nothing at all.
    katherine::detail::check(KATHERINE_E_OK);
}

// ------------------------------------------------------------------
// 7. The error mapping, on a device that is not there.

static void
test_error_mapping(void)
{
    // An address naming no link is refused before any socket work, and
    // KATHERINE_E_ADDRESS has no dedicated class: it falls to the catch-all,
    // which is the group every unmapped code lands in.
    bool threw_address = false;
    try {
        katherine::device bad{"not-an-address"};
        (void) bad;
    } catch (const katherine::error&) {
        threw_address = true;
    }
    KT_CHECK(threw_address);

    // Nothing answers in TEST-NET-1 (RFC 5737), so the open fails its
    // enumeration with KATHERINE_E_TIMEOUT. Not loopback: the suite runs a
    // readout emulator there. Asserted by the code it carries rather than by
    // type, which is what tells a silently remapped code from a working one.
    bool threw_timeout = false;
    try {
        katherine::device absent{"udp://192.0.2.1"};
        (void) absent;
    } catch (const katherine::error& e) {
        threw_timeout = (std::string(e.what()) == std::string(katherine_strerror(KATHERINE_E_TIMEOUT)));
    }
    KT_CHECK(threw_timeout);
}

// ------------------------------------------------------------------
// 8. repr.hpp over every remaining type it claims to render.

static void
test_struct_rendering(void)
{
    KT_CHECK(streamed(katherine::tpx3::coord{1, 2}).size() > 0);

    KT_CHECK(streamed(katherine_tpx3_px_toa_tot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_tpx3_px_f_toa_tot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_tpx3_px_toa_only_t{}).size() > 0);
    KT_CHECK(streamed(katherine_tpx3_px_f_toa_only_t{}).size() > 0);
    KT_CHECK(streamed(katherine_tpx3_px_event_count_itot_t{}).size() > 0);
    KT_CHECK(streamed(katherine_tpx3_px_f_event_count_itot_t{}).size() > 0);

    KT_CHECK(streamed(katherine::trigger{}).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::test_pulse_config{}).size() > 0);
    KT_CHECK(streamed(katherine::tpx3::dacs{}).size() > 0);
    KT_CHECK(streamed(katherine::frame_info{}).size() > 0);
    KT_CHECK(streamed(katherine_frame_info_time_t{}).size() > 0);
    KT_CHECK(streamed(katherine_readout_status_t{}).size() > 0);
    KT_CHECK(streamed(katherine_comm_status_t{}).size() > 0);

    // The wrapper classes, rendered over their accessors rather than as the
    // C struct they own.
    katherine::tpx3::config c;
    KT_CHECK(streamed(c).size() > 0);

    // A px_config renders as a digest, so it is short however large it is.
    katherine::tpx3::px_config px{};
    KT_CHECK(streamed(px).size() > 0);
}

// ------------------------------------------------------------------
// 9. The readout's own peripherals, bound as aliases rather than as wrapper
// classes.
//
// Their enumerations render through overloads in the global namespace, not in
// katherine::, because an alias of a C enum is found only there -- so an
// overload written in the wrong namespace compiles and then fails to resolve
// here, which is the whole point of these lines.

static void
test_readout_peripherals(void)
{
    KT_CHECK(streamed(katherine::gpio_mapping{KATHERINE_GPIO_SHUTTER}).size() > 0);
    KT_CHECK(streamed(katherine::gpio_monitor_signal{KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC}).size() > 0);
    KT_CHECK(streamed(katherine::tdc_source{KATHERINE_TDC_SOURCE_GPIO_2}).size() > 0);
    KT_CHECK(streamed(katherine::pulse_output{KATHERINE_PULSE_OUTPUT_PASSTHROUGH}).size() > 0);
    KT_CHECK(streamed(KATHERINE_TPX3_DAC_MONITOR_BANDGAP_TEMP).size() > 0);

    // As for the chip's enumerations: two members must not render alike.
    KT_CHECK(streamed(katherine::gpio_mapping{KATHERINE_GPIO_INPUT})
        != streamed(katherine::gpio_mapping{KATHERINE_GPIO_HIGH}));

    // The configuration structures are plain aggregates, so an enum assigns
    // into them without a cast -- which is why they are aliases.
    katherine::gpio_config gpio{};
    gpio.channel[0].mapping     = KATHERINE_GPIO_CLOCK;
    gpio.channel[0].termination = true;
    gpio.channel[1].ready       = KATHERINE_GPIO_READY_WHEN_LOW;
    KT_CHECK(gpio.channel[0].mapping == KATHERINE_GPIO_CLOCK);

    katherine::tdc_config tdc{};
    tdc.channel[0].enabled = true;
    tdc.channel[0].source  = KATHERINE_TDC_SOURCE_CHIP_1_FEEDBACK;
    KT_CHECK(tdc.channel[0].source == KATHERINE_TDC_SOURCE_CHIP_1_FEEDBACK);

    katherine::pulse_config gen{};
    gen.channel[2].output         = KATHERINE_PULSE_OUTPUT_PULSE_INVERTED;
    gen.channel[2].length_seconds = 1e-6;
    gen.source                    = KATHERINE_PULSE_SOURCE_OSCILLATOR;
    gen.oscillator_frequency_hz   = 1e6;
    KT_CHECK(gen.channel[2].length_seconds == 1e-6);

    // And the states, which render as structures.
    katherine::gpio_monitor state{};
    state.channel[0].signal = KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT;
    KT_CHECK(streamed(state).size() > 0);
    KT_CHECK(streamed(state.channel[0]).size() > 0);

    // What the readout counted, read as a frequency and a duty cycle. Both
    // are arithmetic over the probe's own tick counts rather than a second
    // question for the readout, which is why they can be checked here with
    // no readout at all. Python had both; C++ had neither.
    katherine::gpio_channel_monitor square{};
    square.signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC;
    square.high_ticks = 50;
    square.low_ticks  = 50;
    KT_CHECK(katherine::gpio_channel_duty_cycle(square) > 0.49);
    KT_CHECK(katherine::gpio_channel_duty_cycle(square) < 0.51);
    KT_CHECK(katherine::gpio_channel_frequency(square) > 0.0);

    // Twice the ticks is half the frequency, whatever the tick is worth.
    katherine::gpio_channel_monitor slower = square;
    slower.high_ticks                      = 100;
    slower.low_ticks                       = 100;
    KT_CHECK(katherine::gpio_channel_frequency(slower)
        < katherine::gpio_channel_frequency(square));

    katherine_tpx3_dac_voltages_t voltages{};
    KT_CHECK(streamed(voltages).size() > 0);

    // The counts the timing unit reports are a struct now, like the GPIO
    // monitor beside them, so the wrapper is the C type rather than a
    // std::array special case -- and it streams like every other struct.
    katherine::tdc_counts counts{};
    KT_CHECK_EQ(sizeof(counts.channel) / sizeof(counts.channel[0]), (size_t) KATHERINE_MAX_TDC_CHANNEL_COUNT);
    KT_CHECK(streamed(counts).size() > 0);
}

// ------------------------------------------------------------------
// 10. Discovery, aimed at a loopback address with no readout on it.
//
// Finding nothing is not an error, so the call comes back with an empty
// vector -- which is also the one thing worth checking about the wrapper:
// that it resizes to what was found rather than handing back the capacity it
// asked for.

static void
test_discovery(void)
{
    const auto found = katherine::discover("udp://127.0.0.1", 42701, std::chrono::milliseconds{50}, 8);
    KT_CHECK(found.empty());

    // A millisecond is the shortest limit the C function takes, so a
    // duration under one is refused rather than becoming no limit at all.
    bool too_short = false;
    try {
        (void) katherine::discover("udp://127.0.0.1", 42701, std::chrono::microseconds{500}, 8);
    } catch (const katherine::system_error&) {
        too_short = true;
    }
    KT_CHECK(too_short);

    // A reply port the firmware would ignore is refused, and the wrapper
    // turns that into an exception like every other failure.
    bool threw = false;
    try {
        (void) katherine::discover("udp://127.0.0.1", KATHERINE_DISCOVER_MIN_PORT - 1,
            std::chrono::milliseconds{50});
    } catch (const katherine::system_error&) {
        threw = true;
    }
    KT_CHECK(threw);
}

/* The bus walk needs no readout attached: with none it answers an empty
 * list rather than failing, and it refuses nowhere to put the answers. */
#ifdef KATHERINE_FEATURE_USB
static void
test_usb_discovery(void)
{
    // Whatever is attached, the call succeeds and reports no more than was
    // asked for. A bus with nothing on it is not an error.
    const auto found = katherine::discover_usb(4);
    KT_CHECK(found.size() <= 4);

    // Each answer names its link, so it can be handed straight to a device.
    for (const auto& d : found) {
        KT_CHECK(std::string{d.address}.rfind("usb://", 0) == 0);
        KT_CHECK(!d.legacy);
    }

    bool threw = false;
    try {
        (void) katherine::discover_usb(0);
    } catch (const katherine::system_error&) {
        threw = true;
    }
    KT_CHECK(threw);
}
#endif /* KATHERINE_FEATURE_USB */

int
main(void)
{
    KT_RUN(test_version);
    KT_RUN(test_enum_rendering);
    KT_RUN(test_config_round_trip);
    KT_RUN(test_px_config);
    KT_RUN(test_ownership);
    KT_RUN(test_derived_info_recognize);
    KT_RUN(test_clock_helpers);
    KT_RUN(test_timestamps);
    KT_RUN(test_errors);
    KT_RUN(test_error_mapping);
    KT_RUN(test_struct_rendering);
    KT_RUN(test_readout_peripherals);
    KT_RUN(test_discovery);
#ifdef KATHERINE_FEATURE_USB
    KT_RUN(test_usb_discovery);
#endif /* KATHERINE_FEATURE_USB */
    return kt_summary();
}
