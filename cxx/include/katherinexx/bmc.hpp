/* Katherine Control Library
 *
 * This file was created on 28.1.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <katherine/bmc.h>

#include <katherinexx/error.hpp>

namespace katherine {

using bmc = katherine_bmc_t;

inline katherine::bmc
load_bmc(std::string file_path)
{
    katherine::bmc data{};
    int res = katherine_bmc_load(&data, file_path.c_str());
    if (res != 0) {
        throw katherine::system_error{res};
    }

    return data;
}

}
