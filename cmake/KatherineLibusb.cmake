# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

#[=======================================================================[.rst:
KatherineLibusb
---------------

Finds libusb-1.0 and exposes it as the imported target ``katherine::libusb``.

``katherine_find_libusb(<out_var>)``
  Sets ``<out_var>`` to whether libusb was found, and defines the target on
  the first call that finds it. Safe to call more than once.

pkg-config is preferred where it exists, since it reports the include
directory the headers actually live in -- ``/usr/include/libusb-1.0`` on most
distributions, which ``#include <libusb.h>`` needs on the path. It is not
required, because the binary release for Windows ships no ``.pc`` file and
Visual Studio hosts have no pkg-config to read one: requiring it made
``-DKATHERINE_BUILD_TRANSPORT_USB=ON`` a configure failure there rather than
a build.

Set ``LIBUSB_ROOT`` to the unpacked binary release to use the fallback, e.g.

  cmake -DKATHERINE_BUILD_TRANSPORT_USB=ON -DLIBUSB_ROOT=C:/libusb-1.0.30
#]=======================================================================]

function(katherine_find_libusb out_var)
    if(TARGET katherine::libusb)
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KATHERINE_LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
        if(TARGET PkgConfig::KATHERINE_LIBUSB)
            add_library(katherine::libusb ALIAS PkgConfig::KATHERINE_LIBUSB)
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    # No pkg-config, or it knows nothing about libusb. The header is flat in
    # the Windows release and under libusb-1.0/ when a distribution installs
    # it, so both are searched; the library sits in a per-toolchain directory
    # in the release, which is why those are spelled out.
    find_path(LIBUSB_INCLUDE_DIR
              NAMES libusb.h
              HINTS "${LIBUSB_ROOT}"
              PATH_SUFFIXES include include/libusb-1.0 libusb-1.0)

    find_library(LIBUSB_LIBRARY
                 NAMES libusb-1.0 usb-1.0
                 HINTS "${LIBUSB_ROOT}"
                 PATH_SUFFIXES lib lib64
                               VS2022/MS64/dll VS2022/MS32/dll
                               VS2019/MS64/dll VS2019/MS32/dll
                               MinGW64/dll MinGW32/dll)

    if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
        add_library(katherine::libusb UNKNOWN IMPORTED)
        set_target_properties(katherine::libusb PROPERTIES
            IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()
