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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

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
    /* .use_trigger = */ false,
    /* .channel = */ KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL,
    /* .edge = */ KATHERINE_TRIGGER_EDGE_RISING,
    /* .delayed = */ false,
    /* .delay_seconds = */ 0.0,
};


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

/// The chip's internal test-pulse generator: whether it fires, and with what shape.
using test_pulse_config = katherine_tpx3_test_pulse_config_t;

/**
 * Timepix3's DACs. Namespaced because which DACs exist, how wide each
 * is and what it biases are all Timepix3's; a second ASIC brings its own.
 */
using dacs = katherine_tpx3_dacs_t;
/// One of the chip's analog monitors, which its single output pad can be pointed at.
using dac_monitor = katherine_tpx3_dac_monitor_t;
/// Every monitor read at once, one Volt reading per monitor.
using dac_voltages = katherine_tpx3_dac_voltages_t;

/// How the chip delivers its hits: at the end of each frame, or as they happen.
enum class readout_mode {
    sequential  = KATHERINE_TPX3_READOUT_SEQUENTIAL,
    data_driven = KATHERINE_TPX3_READOUT_DATA_DRIVEN
};

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

/**
 * Everything katherine::device::configure() sends before an acquisition.
 *
 * The per-chip settings are a flexible array in C, so this allocates rather
 * than embedding: the chip count is fixed at construction and the storage is
 * zeroed, which makes a freshly built configuration the safe one the C
 * structure documents -- no bias, holes collected, no test pulses.
 */
class config {
    struct free_deleter {
        void operator()(void *p) const noexcept { std::free(p); }
    };

    std::unique_ptr<katherine_tpx3_config_t, free_deleter> conf_;
    std::uint8_t chip_count_;

    static std::size_t bytes(std::uint8_t chip_count)
    {
        return KATHERINE_TPX3_CONFIG_SIZE(chip_count);
    }

public:
    /// Builds a configuration for chip_count chips, every setting zeroed.
    explicit config(std::uint8_t chip_count = 1)
        : conf_(static_cast<katherine_tpx3_config_t *>(std::calloc(1, bytes(chip_count)))), chip_count_(chip_count)
    {
        if (conf_ == nullptr) throw std::bad_alloc{};
        conf_->common.chip_type  = KATHERINE_CHIP_TPX3;
        conf_->common.chip_count = chip_count;
    }

    // Copyable and movable on purpose, unlike device and base_acquisition:
    // a caller holding two configurations that differ in one setting is the
    // ordinary use. The copy is deep because the C structure is trivially
    // copyable -- which is why the matrices are embedded rather than
    // referenced.
    /// Copies another configuration, chip settings and matrices included.
    config(const config& other) : config(other.chip_count_)
    {
        std::memcpy(conf_.get(), other.conf_.get(), bytes(chip_count_));
    }

    /// \copydoc config(const config&)
    config& operator=(const config& other)
    {
        config tmp(other);
        conf_.swap(tmp.conf_);
        std::swap(chip_count_, tmp.chip_count_);
        return *this;
    }

    /// Takes over another configuration's storage, leaving it empty.
    config(config&&) noexcept = default;
    /// \copydoc config(config&&)
    config& operator=(config&&) noexcept = default;
    ~config()                            = default;

    /// Chips this configuration was built for.
    std::uint8_t chip_count() const { return chip_count_; }

    /// The C structure this wraps, for the calls katherinexx does not bind.
    katherine_tpx3_config_t *c_config() { return conf_.get(); }
    /// The C structure this wraps, for the calls katherinexx does not bind.
    const katherine_tpx3_config_t *c_config() const { return conf_.get(); }

    // Note: katherine::tpx3::px_config is a standard-layout type whose only
    // subobject is the katherine_tpx3_px_config_t stored in conf_, hence the
    // downcasts below (see the static_assert in px_config.hpp).
    /// The pixel matrix one chip is given, addressed from 0.
    const katherine::tpx3::px_config& pixels(std::uint8_t chip_index) const
    {
        return static_cast<const katherine::tpx3::px_config&>(conf_->chips[chip_index].pixels);
    }
    /// \copybrief pixels()
    katherine::tpx3::px_config& pixels(std::uint8_t chip_index)
    {
        return static_cast<katherine::tpx3::px_config&>(conf_->chips[chip_index].pixels);
    }
    /// Sets the pixel matrix one chip is given.
    void set_pixels(std::uint8_t chip_index, const katherine::tpx3::px_config& pixels) { conf_->chips[chip_index].pixels = pixels; }


    // The trailing return type (rather than a C++14 deduced one) is what
    // keeps this header valid C++11.
    /// How long one frame's shutter stays open.
    auto acq_time() const -> std::chrono::duration<decltype(conf_->common.acq_time), std::nano>
    {
        return std::chrono::duration<decltype(conf_->common.acq_time), std::nano>{conf_->common.acq_time};
    }

    /** \copydoc acq_time() */
    template<typename Rep, typename Period>
    void set_acq_time(std::chrono::duration<Rep, Period> val)
    {
        using namespace std::chrono;
        conf_->common.acq_time = static_cast<double>(duration_cast<nanoseconds>(val).count());
    }

    /// How many frames the acquisition runs for.
    std::uint32_t frame_count() const { return conf_->common.frame_count; }
    /** \copydoc frame_count() */
    void set_frame_count(std::uint32_t val) { conf_->common.frame_count = val; }

    /// Sensor bias voltage for one supply, in Volts, addressed from 0.
    float bias(std::uint8_t index) const { return conf_->common.bias[index]; }
    /// Sets the sensor bias voltage one supply is asked for, in Volts.
    void set_bias(std::uint8_t index, float val) { conf_->common.bias[index] = val; }

    /// What opens the shutter.
    const trigger& start_trigger() const { return conf_->common.start_trigger; }
    /// What opens the shutter.
    trigger& start_trigger() { return conf_->common.start_trigger; }
    /// What opens the shutter.
    void set_start_trigger(const trigger& t) { conf_->common.start_trigger = t; }
    /// What opens the shutter.
    void set_start_trigger(trigger&& t) { conf_->common.start_trigger = t; }


    /// What closes it, where the shutter is not closed by acq_time() alone.
    const trigger& stop_trigger() const { return conf_->common.stop_trigger; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    trigger& stop_trigger() { return conf_->common.stop_trigger; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    void set_stop_trigger(const trigger& t) { conf_->common.stop_trigger = t; }
    /// What closes it, where the shutter is not closed by acq_time() alone.
    void set_stop_trigger(trigger&& t) { conf_->common.stop_trigger = t; }

    /// True to leave one chip's timestamps in its Gray code rather than decoding them.
    bool gray_disable(std::uint8_t chip_index) const { return conf_->chips[chip_index].gray_disable; }
    /// Sets whether one chip's timestamps stay in Gray code.
    void set_gray_disable(std::uint8_t chip_index, bool val) { conf_->chips[chip_index].gray_disable = val; }

    /** \copydoc katherine::polarity */
    katherine::polarity polarity(std::uint8_t chip_index) const
    {
        return (katherine::polarity) conf_->chips[chip_index].polarity;
    }
    /// Sets the carriers one chip's sensor collects.
    void set_polarity(std::uint8_t chip_index, katherine::polarity val)
    {
        conf_->chips[chip_index].polarity = (katherine_polarity_t) val;
    }

    /**
     * Ask for per-double-column clock phase correction. A request only: what
     * actually happens depends on the device and on the phase count, and is
     * reported after the fact by
     * katherine::base_acquisition::phase_correction(), not here.
     */
    bool correct_clock_phase() const { return conf_->correct_clock_phase; }
    /** \copydoc correct_clock_phase() */
    void set_correct_clock_phase(bool val) { conf_->correct_clock_phase = val; }

    /// Pixel-clock phase count asked for; katherine::tpx3::actual_phases() says what is granted.
    phase clock_phase() const { return (phase) conf_->clock_phase; }
    /** \copydoc clock_phase() */
    void set_clock_phase(phase val) { conf_->clock_phase = (katherine_tpx3_phase_t) val; }

    /// Pixel-clock frequency, which decides both the phase count and the fine oscillator.
    freq clock_frequency() const { return (freq) conf_->clock_frequency; }
    /** \copydoc clock_frequency() */
    void set_clock_frequency(freq val) { conf_->clock_frequency = (katherine_tpx3_freq_t) val; }

    /// One chip's eighteen DACs, addressed from 0.
    const tpx3::dacs& dacs(std::uint8_t chip_index) const { return conf_->chips[chip_index].dacs; }
    /// \copybrief dacs()
    tpx3::dacs& dacs(std::uint8_t chip_index) { return conf_->chips[chip_index].dacs; }
    /// Sets one chip's eighteen DACs.
    void set_dacs(std::uint8_t chip_index, const tpx3::dacs& dacs) { conf_->chips[chip_index].dacs = dacs; }

    /// Whether the readout streams hits as they arrive or a frame at a time.
    tpx3::readout_mode readout_mode() const { return (tpx3::readout_mode) conf_->readout_mode; }
    /** \copydoc readout_mode() */
    void set_readout_mode(tpx3::readout_mode val) { conf_->readout_mode = (katherine_tpx3_readout_mode_t) val; }

    /// What a pixel reports, and so which hit type the decoder produces.
    tpx3::px_mode px_mode() const { return (tpx3::px_mode) conf_->px_mode; }
    /** \copydoc px_mode() */
    void set_px_mode(tpx3::px_mode val) { conf_->px_mode = (katherine_tpx3_px_mode_t) val; }

    /// True to run the fine oscillator fast, which only some clock frequencies accept.
    bool fast_vco_enable() const { return conf_->fast_vco_enable; }
    /** \copydoc fast_vco_enable() */
    void set_fast_vco_enable(bool val) { conf_->fast_vco_enable = val; }

    /// The test-pulse generator, which configure() sends last and only when enabled.
    const test_pulse_config& test_pulses() const { return conf_->test_pulses; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    test_pulse_config& test_pulses() { return conf_->test_pulses; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    void set_test_pulses(const test_pulse_config& tp) { conf_->test_pulses = tp; }
    /// The test-pulse generator, which configure() sends last and only when enabled.
    void set_test_pulses(test_pulse_config&& tp) { conf_->test_pulses = tp; }
};

} // namespace tpx3

/** \} */

}
