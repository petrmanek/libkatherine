/**
 * \file
 * \brief C++ wrapper for pixel matrix configuration format.
 * \author Petr Mánek
 * \date 28.1.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

#include <katherine/px_config.h>

#include <katherinexx/error.hpp>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

using bmc = katherine_bmc_t;
using bpc = katherine_bpc_t;

namespace tpx3 {

/**
 * A position in the Timepix3 pixel matrix, which is 256 x 256 -- hence the
 * byte per axis. Namespaced for that reason: Timepix4's matrix is 448 x 512
 * and needs a wider pair, and a coordinate wide enough for both would cost
 * every Timepix2 and Timepix3 hit two bytes it has no use for.
 */
using coord = katherine_tpx3_coord_t;

} // namespace tpx3

struct px_config: katherine_px_config_t {
    void set_test_bit(katherine::tpx3::coord coord, bool enabled) { katherine_px_config_set_test_bit(this, coord, enabled); }
    bool test_bit(katherine::tpx3::coord coord) const { return katherine_px_config_get_test_bit(this, coord); }

    void set_mask_bit(katherine::tpx3::coord coord, bool masked) { katherine_px_config_set_mask_bit(this, coord, masked); }
    bool mask_bit(katherine::tpx3::coord coord) const { return katherine_px_config_get_mask_bit(this, coord); }

    void set_loc_thl(katherine::tpx3::coord coord, std::uint8_t loc_thl) { katherine_px_config_set_loc_thl(this, coord, loc_thl); }
    std::uint8_t loc_thl(katherine::tpx3::coord coord) const { return katherine_px_config_get_loc_thl(this, coord); }
};

// The member functions above are the only addition; the C struct remains
// the sole (standard-layout) subobject, which config.hpp relies upon when
// viewing katherine_config_t::pixel_config through this type.
static_assert(sizeof(px_config) == sizeof(katherine_px_config_t),
    "katherine::px_config must not add data members");

inline katherine::px_config
load_bmc_data(const bmc& bmc_data)
{
    katherine::px_config data{};
    int res = katherine_px_config_load_bmc_data(&data, &bmc_data);
    if (res != 0) {
        throw katherine::system_error{res};
    }

    return data;
}

inline katherine::px_config
load_bmc_file(std::string file_path)
{
    katherine::px_config data{};
    int res = katherine_px_config_load_bmc_file(&data, file_path.c_str());
    if (res != 0) {
        throw katherine::system_error{res};
    }

    return data;
}

inline katherine::px_config
load_bpc_data(const bpc& bpc_data)
{
    katherine::px_config data{};
    int res = katherine_px_config_load_bpc_data(&data, &bpc_data);
    if (res != 0) {
        throw katherine::system_error{res};
    }

    return data;
}

inline katherine::px_config
load_bpc_file(std::string file_path)
{
    katherine::px_config data{};
    int res = katherine_px_config_load_bpc_file(&data, file_path.c_str());
    if (res != 0) {
        throw katherine::system_error{res};
    }

    return data;
}

/** \} */

}
