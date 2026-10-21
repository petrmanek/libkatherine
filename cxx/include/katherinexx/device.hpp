/**
 * \file
 * \brief C++ wrapper for Katherine device communication.
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
#include <string>
#include <vector>

#include <katherine/device.h>
#include <katherine/transport.h>
#include <katherine/status.h>

#include <katherinexx/config.hpp>
#include <katherinexx/error.hpp>
#include <katherinexx/gpio.hpp>
#include <katherinexx/pulse.hpp>
#include <katherinexx/tdc.hpp>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

/**
 * One readout that answered a discovery.
 */
using discovered = katherine_discovered_t;

#ifdef KATHERINE_FEATURE_UDP
/**
 * Ask every readout reachable at an address to identify itself.
 *
 * \param address Address to ask, broadcast or unicast, optionally naming the
 *   link it is on: "udp://192.168.1.255". An address with no scheme is a UDP
 *   address.
 * \param reply_port Port the readouts are to answer on; at least
 *   KATHERINE_DISCOVER_MIN_PORT.
 * \param timeout Limit on the whole call; at least one millisecond.
 * \param capacity Room to make for answers.
 * \return The addresses that answered, in the order the answers arrived.
 *   Each names its link -- "udp://192.168.1.42" -- so one can be handed
 *   straight to katherine::device.
 */
template<typename Rep, typename Period>
inline std::vector<katherine::discovered>
discover(const std::string& address, uint16_t reply_port, std::chrono::duration<Rep, Period> timeout, std::size_t capacity = 32)
{
    using namespace std::chrono;

    // Truncated rather than rounded, so a duration under a millisecond
    // arrives as the zero the C function refuses. A negative one is clamped
    // to that same zero, the cast below turning it into seven weeks
    // otherwise.
    auto timeout_ms = duration_cast<milliseconds>(timeout);
    if (timeout_ms < milliseconds::zero()) {
        timeout_ms = milliseconds::zero();
    }

    std::vector<katherine::discovered> out(capacity);
    std::size_t count = out.size();

    int res = katherine_device_discover_udp(address.c_str(), reply_port,
        (uint32_t) timeout_ms.count(), out.data(), &count);

    if (res != 0) {
        throw katherine::system_error{res};
    }

    out.resize(count);
    return out;
}
#endif /* KATHERINE_FEATURE_UDP */

#ifdef KATHERINE_FEATURE_USB
/**
 * Ask the bus which readouts are attached, and under what address each can
 * be opened.
 *
 * Nothing is opened: the walk reads descriptors and topology only, so it
 * works before a udev rule grants access. That is the difference from the
 * overload taking an address, which has to put a datagram on the wire.
 *
 * \param capacity Room to make for answers.
 * \return The addresses attached, each naming its link -- "usb://2-3" -- so
 *   one can be handed straight to katherine::device.
 */
inline std::vector<katherine::discovered>
discover_usb(std::size_t capacity = 32)
{
    std::vector<katherine::discovered> out(capacity);
    std::size_t count = out.size();

    int res = katherine_device_discover_usb(out.data(), &count);

    if (res != 0) {
        throw katherine::system_error{res};
    }

    out.resize(count);
    return out;
}
#endif /* KATHERINE_FEATURE_USB */

class device {
    katherine_device_t dev_;

public:
    // Flags are a bitmask, so they arrive as the C type rather than an enum
    // class, which would need operators invented for it.
    device(std::string address, uint32_t flags = 0)
    {
        int res = katherine_device_init(&dev_, address.c_str(), flags);
        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    virtual ~device()
    {
        katherine_device_fini(&dev_);
    }

    katherine_device_t *c_dev()
    {
        return &dev_;
    }

    const katherine_device_t *c_dev() const
    {
        return &dev_;
    }


    /**
     * Require command responses to repeat the operation code of their request exactly.
     * \param strict True to require the request's own operation code, false (the default)
     *   to accept the readout firmware's documented substitutions as well; see
     *   katherine_transport_set_strict_ack() for what those are and why they are tolerated
     */
    void
    set_strict_ack(bool strict)
    {
        katherine_transport_set_strict_ack(&dev_.control_transport, strict);
    }

    /**
     * Count command responses the control link discarded as belonging to no request of its own.
     * \return The running count since the device was opened; see
     *   katherine_transport_set_strict_ack()
     */
    std::uint64_t
    stray_command_responses() const
    {
        return dev_.control_transport.stray_command_responses;
    }
    // Always false for now: this asks the readout, not a hardware-type table,
    // because the answer also depends on firmware; wiring up the real query
    // is future work. See katherine_device_can_correct_timestamp_phase().
    bool
    can_correct_timestamp_phase() const
    {
        return katherine_device_can_correct_timestamp_phase(&dev_);
    }

    katherine_readout_status_t
    readout_status()
    {
        katherine_readout_status_t ro_status;
        int res = katherine_get_readout_status(&dev_, &ro_status);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return ro_status;
    }

    katherine_comm_status_t
    comm_status()
    {
        katherine_comm_status_t comm_status;
        int res = katherine_get_comm_status(&dev_, &comm_status);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return comm_status;
    }

    std::string
    chip_id()
    {
        char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
        int res = katherine_get_chip_id(&dev_, chip_id);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return chip_id;
    }

    float
    readout_temperature()
    {
        float temp;
        int res = katherine_get_readout_temperature(&dev_, &temp);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return temp;
    }

    float
    sensor_temperature()
    {
        float temp;
        int res = katherine_get_sensor_temperature(&dev_, &temp);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return temp;
    }

    void
    perform_digital_test()
    {
        int res = katherine_tpx3_perform_digital_test(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_test_pulses(const katherine::test_pulse_config& tp)
    {
        int res = katherine_tpx3_set_test_pulses(&dev_, &tp);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    float
    adc_voltage(unsigned char channel_id)
    {
        float voltage;
        int res = katherine_get_adc_voltage(&dev_, channel_id, &voltage);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return voltage;
    }

    float
    bias(unsigned char bias_id)
    {
        float voltage;
        int res = katherine_get_bias(&dev_, bias_id, &voltage);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return voltage;
    }

    float
    bias_leakage(unsigned char bias_id)
    {
        float voltage;
        int res = katherine_get_bias_leakage(&dev_, bias_id, &voltage);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return voltage;
    }

    float
    dac_monitor_voltage(std::uint8_t chip_index, katherine::tpx3::dac_monitor monitor)
    {
        float voltage;
        int res = katherine_tpx3_get_dac_monitor_voltage(&dev_, chip_index, monitor, &voltage);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return voltage;
    }

    katherine::tpx3::dac_voltages
    dac_monitor_voltages(std::uint8_t chip_index)
    {
        katherine::tpx3::dac_voltages voltages;
        int res = katherine_tpx3_get_dac_monitor_voltages(&dev_, chip_index, &voltages);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return voltages;
    }

    void
    set_gpio_config(const katherine::gpio_config& config)
    {
        int res = katherine_set_gpio_config(&dev_, &config);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_composed_ready_signal(const katherine::gpio_config& config)
    {
        int res = katherine_set_composed_ready_signal(&dev_, &config);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    katherine::gpio_monitor
    probe_gpio()
    {
        katherine::gpio_monitor state;
        int res = katherine_probe_gpio(&dev_, &state);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return state;
    }

    void
    set_shutter_delays(double start_seconds, double stop_seconds)
    {
        int res = katherine_set_shutter_delays(&dev_, start_seconds, stop_seconds);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_tdc_config(const katherine::tdc_config& config)
    {
        int res = katherine_set_tdc_config(&dev_, &config);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    katherine::tdc_counts
    tdc_counts()
    {
        katherine::tdc_counts counts{};
        int res = katherine_get_tdc_counts(&dev_, counts.data());

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return counts;
    }

    void
    set_pulse_generator(const katherine::pulse_config& config)
    {
        int res = katherine_set_pulse_generator(&dev_, &config);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    katherine::pulse_counts
    pulse_generator_counts()
    {
        katherine::pulse_counts state;
        int res = katherine_get_pulse_generator_counts(&dev_, &state);

        if (res != 0) {
            throw katherine::system_error{res};
        }

        return state;
    }

    void
    reset_pulse_generator_counts()
    {
        int res = katherine_reset_pulse_generator_counts(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    // The following methods each drive one step of detector/readout
    // configuration; configure() runs the whole sequence katherine_configure()
    // does in the C API. Kept here (rather than on katherine::config, which
    // only models the parameter struct) to match where the Python wrapper
    // puts the equivalent calls: on the device.

    void
    configure(const katherine::config& config)
    {
        int res = katherine_configure(&dev_, config.c_config());

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_all_pixel_config(const katherine::px_config& px_config)
    {
        int res = katherine_tpx3_set_all_pixel_config(&dev_, &px_config);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    template<typename Rep, typename Period>
    void
    set_acq_time(std::chrono::duration<Rep, Period> val)
    {
        using namespace std::chrono;
        int res = katherine_set_acq_time(&dev_, (double) duration_cast<nanoseconds>(val).count());

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_acq_mode(katherine::tpx3::px_mode mode, bool fast_vco_enabled)
    {
        int res = katherine_tpx3_set_acq_mode(&dev_, (katherine_tpx3_px_mode_t) mode, fast_vco_enabled);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_no_frames(int no_frames)
    {
        int res = katherine_set_no_frames(&dev_, no_frames);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_bias(unsigned char bias_id, float bias_value)
    {
        int res = katherine_set_bias(&dev_, bias_id, bias_value);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_seq_readout_start(int arg)
    {
        int res = katherine_tpx3_set_seq_readout_start(&dev_, arg);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_acquisition_triggers(const katherine::trigger& start_trigger, bool delayed_start, const katherine::trigger& end_trigger)
    {
        int res = katherine_set_acquisition_triggers(&dev_, &start_trigger, delayed_start, &end_trigger);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_sensor_register(katherine_tpx3_reg_t reg_idx, std::int32_t reg_value)
    {
        int res = katherine_tpx3_set_sensor_register(&dev_, (char) reg_idx, reg_value);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    update_sensor_registers()
    {
        int res = katherine_tpx3_update_sensor_registers(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    output_block_config_update()
    {
        int res = katherine_tpx3_output_block_config_update(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    timer_set()
    {
        int res = katherine_tpx3_timer_set(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_dacs(const katherine::tpx3::dacs& dacs)
    {
        int res = katherine_tpx3_set_dacs(&dev_, &dacs);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }
};

/** \} */

}
