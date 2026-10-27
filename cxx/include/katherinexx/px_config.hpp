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

namespace tpx3 {

/// A whole matrix in the BMC layout, as a BurdaMan .bmc file holds it.
using bmc = katherine_tpx3_bmc_t;
/// A whole matrix in the BPC layout, as a Pixet .bpc file holds it.
using bpc = katherine_tpx3_bpc_t;

/**
 * A position in the Timepix3 pixel matrix, which is 256 x 256 -- hence the
 * byte per axis. Namespaced for that reason: Timepix4's matrix is 448 x 512
 * and needs a wider pair, and a coordinate wide enough for both would cost
 * every Timepix2 and Timepix3 hit two bytes it has no use for.
 */
using coord = katherine_tpx3_coord_t;

/**
 * A pixel matrix in the form the readout is uploaded, with per-pixel
 * accessors.
 *
 * Derives from the C structure and adds no members of its own, so a
 * katherine_tpx3_px_config_t can be viewed through this type -- which is
 * what katherine::tpx3::config does with the matrix it holds.
 */
struct px_config: katherine_tpx3_px_config_t {
    /// Whether this pixel fires on a test pulse.
    void set_test_bit(coord coord, bool enabled) { katherine_tpx3_px_config_set_test_bit(this, coord, enabled); }
    /** \copydoc set_test_bit() */
    bool test_bit(coord coord) const { return katherine_tpx3_px_config_get_test_bit(this, coord); }

    /// Whether this pixel is masked off, and so reports nothing.
    void set_mask_bit(coord coord, bool masked) { katherine_tpx3_px_config_set_mask_bit(this, coord, masked); }
    /** \copydoc set_mask_bit() */
    bool mask_bit(coord coord) const { return katherine_tpx3_px_config_get_mask_bit(this, coord); }

    /// This pixel's four-bit threshold trim, which equalisation sets.
    void set_loc_thl(coord coord, std::uint8_t loc_thl) { katherine_tpx3_px_config_set_loc_thl(this, coord, loc_thl); }
    /** \copydoc set_loc_thl() */
    std::uint8_t loc_thl(coord coord) const { return katherine_tpx3_px_config_get_loc_thl(this, coord); }
};

// The member functions above are the only addition; the C struct remains
// the sole (standard-layout) subobject, which config.hpp relies upon when
// viewing katherine_tpx3_chip_config_t::pixels through this type.
static_assert(sizeof(px_config) == sizeof(katherine_tpx3_px_config_t),
    "px_config must not add data members");

/// A pixel matrix read from a BMC matrix already in memory.
inline px_config
load_bmc_data(const bmc& bmc_data)
{
    px_config data{};
    katherine::detail::check(katherine_tpx3_px_config_load_bmc_data(&data, &bmc_data));

    return data;
}

/// A pixel matrix read from a BurdaMan .bmc file.
inline px_config
load_bmc_file(std::string file_path)
{
    px_config data{};
    katherine::detail::check(katherine_tpx3_px_config_load_bmc_file(&data, file_path.c_str()));

    return data;
}

/// A pixel matrix read from a BPC matrix already in memory.
inline px_config
load_bpc_data(const bpc& bpc_data)
{
    px_config data{};
    katherine::detail::check(katherine_tpx3_px_config_load_bpc_data(&data, &bpc_data));

    return data;
}

/// A pixel matrix read from a Pixet .bpc file.
inline px_config
load_bpc_file(std::string file_path)
{
    px_config data{};
    katherine::detail::check(katherine_tpx3_px_config_load_bpc_file(&data, file_path.c_str()));

    return data;
}

} // namespace tpx3

/** \} */

}
