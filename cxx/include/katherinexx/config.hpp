/**
 * \file
 * \brief C++ wrapper for detector and readout configuration.
 * \author Petr Mánek
 * \date 28.1.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>

#include <katherine/config.h>

#include <katherinexx/error.hpp>
#include <katherinexx/px_config.hpp>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

/// One edge-triggered shutter control: which channel, which edge, and any delay.
using trigger = katherine_trigger_t;

/// A disarmed trigger, for an acquisition bounded by time alone.
constexpr trigger no_trigger{
    // TODO: uncomment in C++2a
    /* .enabled = */ false,
    /* .channel = */ 0,
    /* .edge = */ KATHERINE_TRIGGER_EDGE_RISING,
    /* .delayed = */ false,
    /* .delay_seconds = */ 0.0,
};


/// The chip's internal test-pulse generator: whether it fires, and with what shape.
using test_pulse_config = katherine_tpx3_test_pulse_config_t;

/**
 * Charge carriers the sensor collects, and therefore the bias polarity the
 * assembly is operated at. Mirrors katherine_polarity_t, holes being zero for
 * the same reason: a value nobody set must be the one that cannot destroy the
 * chip.
 */
enum class polarity : int {
    holes     = KATHERINE_POLARITY_HOLES,
    electrons = KATHERINE_POLARITY_ELECTRONS
};

/**
 * Settings whose meaning is Timepix3's, gathered so that a second ASIC adds
 * a namespace of its own rather than overloading these names. The C surface
 * spells the same distinction with a tpx3_ prefix, which C++ can express
 * properly.
 */
namespace tpx3 {

/**
 * Timepix3's DACs. Namespaced because which DACs exist, how wide each
 * is and what it biases are all Timepix3's; a second ASIC brings its own.
 */
using dacs = katherine_tpx3_dacs_t;
/// One of the chip's analog monitors, which its single output pad can be pointed at.
using dac_monitor = katherine_tpx3_dac_monitor_t;
/// Every monitor read at once, one Volt reading per monitor.
using dac_voltages = katherine_tpx3_dac_voltages_t;

/// Pixel-clock phase count asked for; Table 17 says what the divider grants.
enum class phase : int {
    p1  = KATHERINE_TPX3_PHASE_1,
    p2  = KATHERINE_TPX3_PHASE_2,
    p4  = KATHERINE_TPX3_PHASE_4,
    p8  = KATHERINE_TPX3_PHASE_8,
    p16 = KATHERINE_TPX3_PHASE_16
};

/// Pixel-clock frequency selector.
enum class freq : int {
    f20  = KATHERINE_TPX3_FREQ_20_MHZ,
    f40  = KATHERINE_TPX3_FREQ_40_MHZ,
    f80  = KATHERINE_TPX3_FREQ_80_MHZ,
    f160 = KATHERINE_TPX3_FREQ_160_MHZ
};

// Lives here rather than in acquisition.hpp (which also uses it, via this
// header) because katherine_tpx3_px_mode_t is itself declared in
// katherine/config.h; keeping the two together lets katherine::device use
// the enum class without pulling in acquisition.hpp.
/// What a pixel measures, and so the shape of the hits it produces.
enum class px_mode : int {
    toa_tot          = KATHERINE_TPX3_PX_TOA_TOT,
    only_toa         = KATHERINE_TPX3_PX_ONLY_TOA,
    event_count_itot = KATHERINE_TPX3_PX_EVENT_COUNT_ITOT
};

/**
 * Phases the clock divider actually grants, which is not what the enumerator
 * asks for: Timepix3 manual Table 17 clamps the count at the faster
 * frequencies. Anything dividing a coarse tick by the phase count has to
 * divide by this.
 */
inline std::uint8_t
actual_phases(freq f, phase p)
{
    return katherine_tpx3_actual_phases((katherine_tpx3_freq_t) f, (katherine_tpx3_phase_t) p);
}

/// True where the fine oscillator can run, which only the slowest pixel clock allows.
inline bool
freq_is_fast_vco_supported(freq f)
{
    return katherine_tpx3_freq_is_fast_vco_supported((katherine_tpx3_freq_t) f);
}

/// The largest value a DAC takes, Timepix3 manual Table 11.
inline std::uint16_t
dac_max(katherine_tpx3_dac_t dac)
{
    return katherine_tpx3_dac_max(dac);
}

/**
 * A DAC's digital value in SI units.
 *
 * Which unit that is depends on the DAC -- some bias a current and some a
 * voltage -- so it comes back alongside the number rather than being
 * assumed.
 */
inline double
dac_to_si(katherine_tpx3_dac_t dac, std::uint16_t value, katherine_dac_unit_t& unit)
{
    return katherine_tpx3_dac_to_si(dac, value, &unit);
}

/**
 * Checks every DAC against its own width, Timepix3 manual Table 11.
 *
 * Opt-in, as in C: device::set_dacs() transmits what it is given, and a value
 * too wide for its DAC is truncated by the chip rather than refused.
 */
inline void
validate(const dacs& v)
{
    katherine::detail::check(katherine_tpx3_dacs_validate(&v));
}

} // namespace tpx3

/**
 * Everything katherine::device::configure() sends before an acquisition.
 *
 * Holds one katherine_config_t and nothing else, value-initialized, so a
 * default-constructed configuration is the safe one the C structure
 * documents: no bias, holes collected, no test pulses.
 */
class config {
    // Value-initialized: the defaulted constructor below would otherwise
    // leave this indeterminate.
    katherine_config_t conf_{};

public:
    // Copyable and movable on purpose, unlike device and base_acquisition:
    // this owns nothing but a katherine_config_t, and a caller holding two
    // configurations that differ in one setting is the ordinary use. The
    // destructor is non-virtual for the same reason -- nothing derives from
    // it, and a vptr would make a value type carry one for nobody.
    config()  = default;
    ~config() = default;

    /// The C structure this wraps, for the calls katherinexx does not bind.
    katherine_config_t *c_config() { return &conf_; }
    /// The C structure this wraps, for the calls katherinexx does not bind.
    const katherine_config_t *c_config() const { return &conf_; }

    // Note: katherine::px_config is a standard-layout type whose only
    // subobject is the katherine_tpx3_px_config_t stored in conf_, hence the
    // downcasts below (see the static_assert in px_config.hpp).
    /// The pixel matrix, which configure() uploads to chip 0.
    const katherine::px_config& pixel_config() const { return static_cast<const katherine::px_config&>(conf_.pixel_config); }
    /// The pixel matrix, which configure() uploads to chip 0.
    katherine::px_config& pixel_config() { return static_cast<katherine::px_config&>(conf_.pixel_config); }
    /// The pixel matrix, which configure() uploads to chip 0.
    void set_pixel_config(const katherine::px_config& pixel_config) { conf_.pixel_config = pixel_config; }
    /// The pixel matrix, which configure() uploads to chip 0.
    void set_pixel_config(katherine::px_config&& pixel_config) { conf_.pixel_config = pixel_config; }

    /// Which bias supply of the readout this configuration drives.
    std::uint8_t bias_id() const { return conf_.bias_id; }
    /** \copydoc bias_id() */
    void set_bias_id(std::uint8_t val) { conf_.bias_id = val; }

    // The trailing return type (rather than a C++14 deduced one) is what
    // keeps this header valid C++11.
    /// How long one frame's shutter stays open.
    auto acq_time() const -> std::chrono::duration<decltype(conf_.acq_time), std::nano>
    {
        return std::chrono::duration<decltype(conf_.acq_time), std::nano>{conf_.acq_time};
    }

    /** \copydoc acq_time() */
    template<typename Rep, typename Period>
    void set_acq_time(std::chrono::duration<Rep, Period> val)
    {
        using namespace std::chrono;
        conf_.acq_time = static_cast<double>(duration_cast<nanoseconds>(val).count());
    }

    /// How many frames the acquisition runs for.
    int no_frames() const { return conf_.no_frames; }
    /** \copydoc no_frames() */
    void set_no_frames(int val) { conf_.no_frames = val; }

    /// Sensor bias voltage, in Volts, which the readout supplies.
    float bias() const { return conf_.bias; }
    /** \copydoc bias() */
    void set_bias(float val) { conf_.bias = val; }

    /// What opens the shutter.
    const trigger& start_trigger() const { return conf_.start_trigger; }
    /// What opens the shutter.
    trigger& start_trigger() { return conf_.start_trigger; }
    /// What opens the shutter.
    void set_start_trigger(const trigger& t) { conf_.start_trigger = t; }
    /// What opens the shutter.
    void set_start_trigger(trigger&& t) { conf_.start_trigger = t; }


    /// What closes it, where the shutter is not closed by acq_time() alone.
    const trigger& stop_trigger() const { return conf_.stop_trigger; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    trigger& stop_trigger() { return conf_.stop_trigger; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    void set_stop_trigger(const trigger& t) { conf_.stop_trigger = t; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    void set_stop_trigger(trigger&& t) { conf_.stop_trigger = t; }

    /// True to leave timestamps in the chip's Gray code rather than decoding them.
    bool gray_disable() const { return conf_.gray_disable; }
    /** \copydoc gray_disable() */
    void set_gray_disable(bool val) { conf_.gray_disable = val; }

    /** \copydoc katherine::polarity */
    katherine::polarity polarity() const { return (katherine::polarity) conf_.polarity; }
    /** \copydoc katherine::polarity */
    void set_polarity(katherine::polarity val) { conf_.polarity = (katherine_polarity_t) val; }

    /**
     * Ask for per-double-column clock phase correction. A request only: what
     * actually happens depends on the device and on the phase count, and is
     * reported after the fact by
     * katherine::base_acquisition::phase_correction(), not here.
     */
    bool correct_phase() const { return conf_.correct_phase; }
    /** \copydoc correct_phase() */
    void set_correct_phase(bool val) { conf_.correct_phase = val; }

    /// Pixel-clock phase count asked for; katherine::tpx3::actual_phases() says what is granted.
    katherine::tpx3::phase phase() const { return (katherine::tpx3::phase) conf_.phase; }
    /** \copydoc phase() */
    void set_phase(katherine::tpx3::phase val) { conf_.phase = (katherine_tpx3_phase_t) val; }

    /// Pixel-clock frequency, which decides both the phase count and the fine oscillator.
    katherine::tpx3::freq freq() const { return (katherine::tpx3::freq) conf_.freq; }
    /** \copydoc freq() */
    void set_freq(katherine::tpx3::freq val) { conf_.freq = (katherine_tpx3_freq_t) val; }

    /// The chip's eighteen DACs, which configure() writes to chip 0.
    const katherine::tpx3::dacs& dacs() const { return conf_.dacs; }
    /// The chip's eighteen DACs, which configure() writes to chip 0.
    katherine::tpx3::dacs& dacs() { return conf_.dacs; }
    /// The chip's eighteen DACs, which configure() writes to chip 0.
    void set_dacs(const katherine::tpx3::dacs& dacs) { conf_.dacs = dacs; }
    /// The chip's eighteen DACs, which configure() writes to chip 0.
    void set_dacs(katherine::tpx3::dacs&& dacs) { conf_.dacs = dacs; }

    /// The test-pulse generator, which configure() sends last and only when enabled.
    const katherine::test_pulse_config& test_pulse_config() const { return conf_.test_pulse_config; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    katherine::test_pulse_config& test_pulse_config() { return conf_.test_pulse_config; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    void set_test_pulse_config(const katherine::test_pulse_config& tp) { conf_.test_pulse_config = tp; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    void set_test_pulse_config(katherine::test_pulse_config&& tp) { conf_.test_pulse_config = tp; }
};

/** \} */

}
