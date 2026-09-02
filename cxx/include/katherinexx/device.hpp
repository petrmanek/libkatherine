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

#include <katherine/device.h>
#include <katherine/status.h>

#include <katherinexx/config.hpp>
#include <katherinexx/error.hpp>

namespace katherine {

/**
 * \addtogroup cxx_api
 * \{
 */

class device {
    katherine_device_t dev_;

public:
    device(std::string addr)
    {
        int res = katherine_device_init(&dev_, addr.c_str());
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
        int res = katherine_perform_digital_test(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_test_pulses(const katherine::test_pulse_config& tp)
    {
        int res = katherine_set_test_pulses(&dev_, &tp);

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
        int res = katherine_set_all_pixel_config(&dev_, &px_config);

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
    set_acq_mode(katherine::acq_mode mode, bool fast_vco_enabled)
    {
        int res = katherine_set_acq_mode(&dev_, (katherine_acquisition_mode_t) mode, fast_vco_enabled);

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
        int res = katherine_set_seq_readout_start(&dev_, arg);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    acquisition_setup(const katherine::trigger& start_trigger, bool delayed_start, const katherine::trigger& end_trigger)
    {
        int res = katherine_acquisition_setup(&dev_, &start_trigger, delayed_start, &end_trigger);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_sensor_register(katherine_tpx3_reg_t reg_idx, std::int32_t reg_value)
    {
        int res = katherine_set_sensor_register(&dev_, (char) reg_idx, reg_value);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    update_sensor_registers()
    {
        int res = katherine_update_sensor_registers(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    output_block_config_update()
    {
        int res = katherine_output_block_config_update(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    timer_set()
    {
        int res = katherine_timer_set(&dev_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_dacs(const katherine::dacs& dacs)
    {
        int res = katherine_set_dacs(&dev_, &dacs);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }
};

/** \} */

}
