/* Katherine Control Library
 *
 * This file was created on 28.1.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <cstring>

#include <katherine/acquisition.h>

namespace katherine {

class error: public std::runtime_error {
public:
    error(std::string what) :runtime_error{what} { }

};

class system_error: public error {
public:
    system_error(int rc) :error{std::strerror(rc)} { }

};

}
