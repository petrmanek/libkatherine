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

#include <katherine/config.h>

#include <katherinexx/px_config.hpp>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

using trigger = katherine_trigger_t;

static constexpr trigger no_trigger{
    // TODO: uncomment in C++2a
    /* .enabled = */ false,
    /* .channel = */ 0,
    /* .use_falling_edge = */ 0,
};

using dacs = katherine_dacs_t;

using test_pulse_config = katherine_test_pulse_config_t;

/// Settings whose meaning is Timepix3's, gathered so that a second ASIC adds
/// a namespace of its own rather than overloading these names. The C surface
/// spells the same distinction with a tpx3_ prefix, which C++ can express
/// properly.
namespace tpx3 {

enum class phase : int {
    p1  = KATHERINE_TPX3_PHASE_1,
    p2  = KATHERINE_TPX3_PHASE_2,
    p4  = KATHERINE_TPX3_PHASE_4,
    p8  = KATHERINE_TPX3_PHASE_8,
    p16 = KATHERINE_TPX3_PHASE_16
};

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
enum class px_mode : int {
    toa_tot          = KATHERINE_TPX3_PX_TOA_TOT,
    only_toa         = KATHERINE_TPX3_PX_ONLY_TOA,
    event_count_itot = KATHERINE_TPX3_PX_EVENT_COUNT_ITOT
};

} // namespace tpx3

class config {
    katherine_config_t conf_;

public:
    config()          = default;
    virtual ~config() = default;

    katherine_config_t *c_config() { return &conf_; }
    const katherine_config_t *c_config() const { return &conf_; }

    // Note: katherine::px_config is a standard-layout type whose only
    // subobject is the katherine_px_config_t stored in conf_, hence the
    // downcasts below (see the static_assert in px_config.hpp).
    const katherine::px_config& pixel_config() const { return static_cast<const katherine::px_config&>(conf_.pixel_config); }
    katherine::px_config& pixel_config() { return static_cast<katherine::px_config&>(conf_.pixel_config); }
    void set_pixel_config(const katherine::px_config& pixel_config) { conf_.pixel_config = pixel_config; }
    void set_pixel_config(katherine::px_config&& pixel_config) { conf_.pixel_config = pixel_config; }

    unsigned char bias_id() const { return conf_.bias_id; }
    void set_bias_id(unsigned char val) { conf_.bias_id = val; }

    // The trailing return type (rather than a C++14 deduced one) is what
    // keeps this header valid C++11.
    auto acq_time() const -> std::chrono::duration<decltype(conf_.acq_time), std::nano>
    {
        return std::chrono::duration<decltype(conf_.acq_time), std::nano>{conf_.acq_time};
    }

    template<typename Rep, typename Period>
    void set_acq_time(std::chrono::duration<Rep, Period> val)
    {
        using namespace std::chrono;
        conf_.acq_time = static_cast<double>(duration_cast<nanoseconds>(val).count());
    }

    int no_frames() const { return conf_.no_frames; }
    void set_no_frames(int val) { conf_.no_frames = val; }

    float bias() const { return conf_.bias; }
    void set_bias(float val) { conf_.bias = val; }

    const trigger& start_trigger() const { return conf_.start_trigger; }
    trigger& start_trigger() { return conf_.start_trigger; }
    void set_start_trigger(const trigger& t) { conf_.start_trigger = t; }
    void set_start_trigger(trigger&& t) { conf_.start_trigger = t; }

    bool delayed_start() const { return conf_.delayed_start; }
    void set_delayed_start(bool val) { conf_.delayed_start = val; }

    const trigger& stop_trigger() const { return conf_.stop_trigger; }
    trigger& stop_trigger() { return conf_.stop_trigger; }
    void set_stop_trigger(const trigger& t) { conf_.stop_trigger = t; }
    void set_stop_trigger(trigger&& t) { conf_.stop_trigger = t; }

    bool gray_disable() const { return conf_.gray_disable; }
    void set_gray_disable(bool val) { conf_.gray_disable = val; }

    bool polarity_holes() const { return conf_.polarity_holes; }
    void set_polarity_holes(bool val) { conf_.polarity_holes = val; }

    // A request only: what actually happens depends on the device and on the
    // phase count, and is reported after the fact by
    // katherine::base_acquisition::phase_correction(), not by this getter.
    bool correct_phase() const { return conf_.correct_phase; }
    void set_correct_phase(bool val) { conf_.correct_phase = val; }

    katherine::tpx3::phase phase() const { return (katherine::tpx3::phase) conf_.phase; }
    void set_phase(katherine::tpx3::phase val) { conf_.phase = (katherine_tpx3_phase_t) val; }

    katherine::tpx3::freq freq() const { return (katherine::tpx3::freq) conf_.freq; }
    void set_freq(katherine::tpx3::freq val) { conf_.freq = (katherine_tpx3_freq_t) val; }

    const katherine::dacs& dacs() const { return conf_.dacs; }
    katherine::dacs& dacs() { return conf_.dacs; }
    void set_dacs(const katherine::dacs& dacs) { conf_.dacs = dacs; }
    void set_dacs(katherine::dacs&& dacs) { conf_.dacs = dacs; }

    const katherine::test_pulse_config& test_pulse_config() const { return conf_.test_pulse_config; }
    katherine::test_pulse_config& test_pulse_config() { return conf_.test_pulse_config; }
    void set_test_pulse_config(const katherine::test_pulse_config& tp) { conf_.test_pulse_config = tp; }
    void set_test_pulse_config(katherine::test_pulse_config&& tp) { conf_.test_pulse_config = tp; }
};

/** \} */

}
