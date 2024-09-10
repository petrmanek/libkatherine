/* Katherine Control Library
 *
 * This file was created on 28.1.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <string>

#include <katherine/device.h>
#include <katherine/status.h>

#include <katherinexx/error.hpp>

namespace katherine {

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

    katherine_device_t* c_dev()
    {
        return &dev_;
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

};

}
