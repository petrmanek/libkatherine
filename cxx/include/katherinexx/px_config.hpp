/* Katherine Control Library
 *
 * This file was created on 28.1.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <katherine/px_config.h>

#include <katherinexx/error.hpp>

namespace katherine {

using bmc = katherine_bmc_t;
using bpc = katherine_bpc_t;
using coord = katherine_coord_t;
using px_config = katherine_px_config_t;

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

inline void
set_test_bit(katherine::px_config& px_config, katherine::coord coord, bool enabled)
{
    katherine_px_config_set_test_bit(&px_config, coord, enabled);
}

inline bool
get_test_bit(const katherine::px_config& px_config, katherine::coord coord)
{
    return katherine_px_config_get_test_bit(&px_config, coord);
}

inline void
set_mask_bit(katherine::px_config& px_config, katherine::coord coord, bool masked)
{
    katherine_px_config_set_mask_bit(&px_config, coord, masked);
}

inline bool
get_mask_bit(const katherine::px_config& px_config, katherine::coord coord)
{
    return katherine_px_config_get_mask_bit(&px_config, coord);
}

inline void
set_loc_thl(katherine::px_config& px_config, katherine::coord coord, uint8_t loc_thl)
{
    katherine_px_config_set_loc_thl(&px_config, coord, loc_thl);
}

inline uint8_t
get_loc_thl(const katherine::px_config& px_config, katherine::coord coord)
{
    return katherine_px_config_get_loc_thl(&px_config, coord);
}

}
