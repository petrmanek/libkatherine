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

    katherine::detail::check(katherine_device_discover_udp(address.c_str(), reply_port,
        (uint32_t) timeout_ms.count(), out.data(), &count));

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

    katherine::detail::check(katherine_device_discover_usb(out.data(), &count));

    out.resize(count);
    return out;
}
#endif /* KATHERINE_FEATURE_USB */

/**
 * One readout, open for the lifetime of the object.
 *
 * Every method here reaches the readout over its control link and throws
 * katherine::error if it does not answer. A chip-addressed one takes the
 * index first and never by default: a call that does not name a chip says
 * nothing about which one it configured, and on a two-chip readout that
 * silence is a bug waiting to be written.
 */
class device {
    katherine_device_t dev_;

public:
    /**
     * Opens a readout named by address, and by default asks it what it is.
     *
     * Flags are a bitmask, so they arrive as the C type rather than an enum
     * class, which would need operators invented for it.
     *
     * \param address The readout's address, e.g. "udp://192.168.1.142" or "usb://auto".
     * \param flags katherine_device_flags_t, each suppressing one thing the open does.
     */
    device(std::string address, uint32_t flags = 0)
    {
        katherine::detail::check(katherine_device_init(&dev_, address.c_str(), flags));
    }

    virtual ~device()
    {
        katherine_device_fini(&dev_);
    }

    /**
     * Neither copied nor moved: it owns an open link.
     *
     * A copy would finalize the same sockets twice, and a move would leave
     * any acquisition built on this device holding the address it used to
     * be at. Both compiled before this was written, and neither said so
     * until the second katherine_device_fini().
     */
    device(const device&)            = delete;
    device& operator=(const device&) = delete;

    /// The C structure this wraps, for the calls katherinexx does not bind.
    katherine_device_t *c_dev()
    {
        return &dev_;
    }

    /// The C structure this wraps, for the calls katherinexx does not bind.
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

    /**
     * The OS-level code behind the control link's last failure.
     *
     * What every \retval line in this library means by "see
     * katherine_transport_get_last_os_error()": an errno on POSIX, a
     * WSAGetLastError() value on Windows, a libusb code over USB. Meaningful
     * only directly after a call that failed, and 0 where nothing has.
     */
    int
    last_os_error() const
    {
        return katherine_transport_get_last_os_error(&dev_.control_transport);
    }

    /**
     * The limit a control exchange waits for the readout within.
     *
     * Every command of this library is bounded by it, so shortening it makes
     * a readout that is not there fail sooner and a slow one fail wrongly.
     */
    void
    set_timeout(std::chrono::milliseconds timeout)
    {
        katherine::detail::check(katherine_transport_set_timeout(
            &dev_.control_transport, (std::uint32_t) timeout.count()));
    }
    /// What the readout said it is. Zeroed until it has answered, and
    /// constructing a device succeeds either way, the enumeration inside it
    /// being best-effort.
    const katherine_device_info_t& info() const
    {
        return dev_.info;
    }

    /// What this library derives from katherine_device_info_t. Its
    /// `supported` member is also how a caller learns the readout never
    /// answered.
    const katherine_device_derived_info_t& derived_info() const
    {
        return dev_.derived_info;
    }

    /**
     * Asks the readout what it is, filling in what the generation-dependent
     * calls need.
     *
     * The constructor does this unless KATHERINE_DEVICE_DONT_ENUMERATE was
     * given; a readout that was not yet listening then leaves it undone, and
     * an un-enumerated device refuses those calls rather than guessing a
     * generation.
     */
    void
    enumerate()
    {
        katherine::detail::check(katherine_device_enumerate(&dev_));
    }

    /**
     * Takes the caller's word for what this readout is, instead of asking it.
     *
     * The only way to run the generation-dependent calls on a device opened
     * with KATHERINE_DEVICE_DONT_ENUMERATE, and the only way to drive a
     * readout that does not answer the enumeration at all.
     */
    void
    declare(const katherine_device_info_t& info)
    {
        katherine::detail::check(katherine_device_declare(&dev_, &info));
    }

    /**
     * Tells the readout which interface drives it.
     *
     * The constructor does this unless KATHERINE_DEVICE_DONT_SELECT_INTERFACE
     * was given. A readout that wants it and has not been told answers every
     * later command orders of magnitude more slowly, and will not start an
     * acquisition over UDP at all.
     */
    void
    select_interface()
    {
        katherine::detail::check(katherine_device_select_interface(&dev_));
    }

    /**
     * True on a readout that can apply the per-double-column phase
     * correction itself, leaving the decoder nothing to do.
     *
     * Always false for now: this asks the readout, not a hardware-type
     * table, because the answer also depends on firmware; wiring up the real
     * query is future work. Costs no round trip either way.
     */
    bool
    can_correct_timestamp_phase() const
    {
        return katherine_device_can_correct_timestamp_phase(&dev_);
    }

    /// What the readout answers when asked to identify itself.
    katherine_readout_status_t
    readout_status()
    {
        katherine_readout_status_t ro_status;
        katherine::detail::check(katherine_get_readout_status(&dev_, &ro_status));

        return ro_status;
    }


    /**
     * Asks one chip what it kept of the configuration it was given.
     *
     * The two flags are the chip's own read-back, not this library's: a
     * matrix that came back with a parity error, or a DAC that did not read
     * as written, is reported here and nowhere else. Second generation only.
     */
    katherine_tpx3_config_check_t
    verify_config(std::uint8_t chip_index)
    {
        katherine_tpx3_config_check_t check;
        katherine::detail::check(katherine_tpx3_verify_config(&dev_, chip_index, &check));

        return check;
    }

    /**
     * Sets how many tokens the readout gives the chip per shutter.
     *
     * Deliberately not part of configure(), which leaves whatever the readout
     * holds: this is the only way to choose it.
     */
    void
    set_token_count(std::uint8_t token_count)
    {
        katherine::detail::check(katherine_tpx3_set_token_count(&dev_, token_count));
    }

    /// How the readout's link to its chips is doing, and how many answered.
    katherine_comm_status_t
    comm_status()
    {
        katherine_comm_status_t comm_status;
        katherine::detail::check(katherine_get_comm_status(&dev_, &comm_status));

        return comm_status;
    }


    /// Asks one chip of the readout for its own identifier.
    std::string
    chip_id(std::uint8_t chip_index)
    {
        char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
        katherine::detail::check(katherine_get_chip_id(&dev_, chip_index, chip_id));

        return chip_id;
    }

    /// The readout board's own temperature, in Celsius, which is safe to poll during an acquisition.
    float
    readout_temperature()
    {
        float temp;
        katherine::detail::check(katherine_get_readout_temperature(&dev_, &temp));

        return temp;
    }


    /// Measures one chip's own on-die temperature sensor.
    float
    sensor_temperature(std::uint8_t chip_index)
    {
        float temp;
        katherine::detail::check(katherine_get_sensor_temperature(&dev_, chip_index, &temp));

        return temp;
    }


    /// Tests the link between the readout and one of its chips.
    void
    perform_digital_test(std::uint8_t chip_index)
    {
        katherine::detail::check(katherine_tpx3_perform_digital_test(&dev_, chip_index));
    }

    /// Arms or disarms the chip's internal test-pulse generator.
    void
    set_test_pulses(const katherine::test_pulse_config& tp)
    {
        katherine::detail::check(katherine_tpx3_set_test_pulses(&dev_, &tp));
    }

    /// One channel of the readout's analog-to-digital converter, in Volts.
    float
    adc_voltage(std::uint8_t channel_id)
    {
        float voltage;
        katherine::detail::check(katherine_get_adc_voltage(&dev_, channel_id, &voltage));

        return voltage;
    }

    /// What one bias supply is actually delivering, in Volts.
    float
    bias(std::uint8_t bias_index)
    {
        float voltage;
        katherine::detail::check(katherine_get_bias(&dev_, bias_index, &voltage));

        return voltage;
    }

    /// The leakage current flowing through the sensor, in microamperes. The
    /// zero point is not calibrated per unit, so readings carry an offset of
    /// up to about 0.4 uA.
    float
    bias_leakage(std::uint8_t bias_index)
    {
        float current;
        katherine::detail::check(katherine_get_bias_leakage(&dev_, bias_index, &current));

        return current;
    }

    /// One chip's analog output pad, pointed at one monitor and read, in Volts.
    float
    dac_monitor_voltage(std::uint8_t chip_index, katherine::tpx3::dac_monitor monitor)
    {
        float voltage;
        katherine::detail::check(katherine_tpx3_get_dac_monitor_voltage(&dev_, chip_index, monitor, &voltage));

        return voltage;
    }

    /// Every monitor of one chip, read in one sweep.
    katherine::tpx3::dac_voltages
    dac_monitor_voltages(std::uint8_t chip_index)
    {
        katherine::tpx3::dac_voltages voltages;
        katherine::detail::check(katherine_tpx3_get_dac_monitor_voltages(&dev_, chip_index, &voltages));

        return voltages;
    }

    /// Wires each GPIO channel to what it is to carry.
    void
    set_gpio_config(const katherine::gpio_config& config)
    {
        katherine::detail::check(katherine_set_gpio_config(&dev_, &config));
    }

    /// Says which channels take part in the readout's composed ready signal.
    void
    set_composed_ready_signal(const katherine::gpio_config& config)
    {
        katherine::detail::check(katherine_set_composed_ready_signal(&dev_, &config));
    }

    /// Samples every GPIO channel: its level, what the readout makes of it, and its tick counts.
    katherine::gpio_monitor
    probe_gpio()
    {
        katherine::gpio_monitor state;
        katherine::detail::check(katherine_probe_gpio(&dev_, &state));

        return state;
    }

    /// Says what each channel of the timing unit watches.
    void
    set_tdc_config(const katherine::tdc_config& config)
    {
        katherine::detail::check(katherine_set_tdc_config(&dev_, &config));
    }

    /// What each timing-unit channel has counted since it was last reset.
    katherine::tdc_counts
    tdc_counts()
    {
        katherine::tdc_counts counts{};
        katherine::detail::check(katherine_get_tdc_counts(&dev_, &counts));

        return counts;
    }

    /// Says what each generator channel emits, and what clocks them.
    void
    set_pulse_generator(const katherine::pulse_config& config)
    {
        katherine::detail::check(katherine_set_pulse_generator(&dev_, &config));
    }

    /// What each generator channel has emitted.
    katherine::pulse_counts
    pulse_generator_counts()
    {
        katherine::pulse_counts state;
        katherine::detail::check(katherine_get_pulse_generator_counts(&dev_, &state));

        return state;
    }

    /// Zeroes those counts.
    void
    reset_pulse_generator_counts()
    {
        katherine::detail::check(katherine_reset_pulse_generator_counts(&dev_));
    }

    // The following methods each drive one step of detector/readout
    // configuration; configure() runs the whole sequence katherine_configure()
    // does in the C API. Kept here (rather than on katherine::config, which
    // only models the parameter struct) to match where the Python wrapper
    // puts the equivalent calls: on the device.

    /// Sends a whole configuration, step by step, to every chip it declares.
    void
    configure(const katherine::config& config)
    {
        katherine::detail::check(katherine_tpx3_configure(&dev_, config.c_config()));
    }


    /// Uploads a whole pixel matrix to one chip of the readout.
    void
    set_all_pixel_config(std::uint8_t chip_index, const katherine::px_config& px_config)
    {
        katherine::detail::check(katherine_tpx3_set_all_pixel_config(&dev_, chip_index, &px_config));
    }

    /// How long one frame's shutter stays open.
    template<typename Rep, typename Period>
    void
    set_acq_time(std::chrono::duration<Rep, Period> val)
    {
        using namespace std::chrono;
        katherine::detail::check(katherine_set_acq_time(&dev_, (double) duration_cast<nanoseconds>(val).count()));
    }

    /// What a pixel measures, and whether the fine oscillator runs.
    void
    set_acq_mode(katherine::tpx3::px_mode mode, bool fast_vco_enabled)
    {
        katherine::detail::check(katherine_tpx3_set_acq_mode(&dev_, (katherine_tpx3_px_mode_t) mode, fast_vco_enabled));
    }

    /// How many frames the next acquisition runs for.
    void
    set_frame_count(std::uint32_t frame_count)
    {
        katherine::detail::check(katherine_set_frame_count(&dev_, frame_count));
    }

    /// Asks one bias supply for a voltage. It arrives seconds later; the acknowledgement is of the command.
    void
    set_bias(std::uint8_t bias_index, float bias_value)
    {
        katherine::detail::check(katherine_set_bias(&dev_, bias_index, bias_value));
    }

    /// Which readout mode the chip uses to deliver hits.
    void
    set_seq_readout_start(int arg)
    {
        katherine::detail::check(katherine_tpx3_set_seq_readout_start(&dev_, arg));
    }

    /// What opens and closes the shutter.
    void
    set_acquisition_triggers(const katherine::trigger& start_trigger, const katherine::trigger& stop_trigger)
    {
        katherine::detail::check(katherine_set_acquisition_triggers(&dev_, &start_trigger, &stop_trigger));
    }


    /// Writes one sensor register of one chip into the readout's register image.
    void
    set_sensor_register(std::uint8_t chip_index, katherine_tpx3_reg_t reg_idx, std::int32_t reg_value)
    {
        katherine::detail::check(
            katherine_tpx3_set_sensor_register(&dev_, chip_index, reg_idx, reg_value));
    }


    /// Carries one chip's register image to its sensor.
    void
    update_sensor_registers(std::uint8_t chip_index)
    {
        katherine::detail::check(katherine_tpx3_update_sensor_registers(&dev_, chip_index));
    }


    /// Latches one chip's output block configuration.
    void
    output_block_config_update(std::uint8_t chip_index)
    {
        katherine::detail::check(katherine_tpx3_output_block_config_update(&dev_, chip_index));
    }


    /// Sets one chip's timer.
    void
    timer_set(std::uint8_t chip_index)
    {
        katherine::detail::check(katherine_tpx3_timer_set(&dev_, chip_index));
    }


    /// Writes the whole DAC block to one chip of the readout.
    void
    set_dacs(std::uint8_t chip_index, const katherine::tpx3::dacs& dacs)
    {
        katherine::detail::check(katherine_tpx3_set_dacs(&dev_, chip_index, &dacs));
    }

    /// Writes one DAC of chip zero, leaving the other seventeen alone.
    void
    set_dac(katherine_tpx3_dac_t dac, std::uint16_t value)
    {
        set_dac(0, dac, value);
    }

    /// Writes one DAC of one chip, leaving the other seventeen alone.
    void
    set_dac(std::uint8_t chip_index, katherine_tpx3_dac_t dac, std::uint16_t value)
    {
        katherine::detail::check(katherine_tpx3_set_dac(&dev_, chip_index, dac, value));
    }
};

/** \} */

}
