# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

set(KATHERINE_SOURCES
    "src/acquisition.c"
    "src/px_config.c"
    "src/config.c"
    "src/device.c"
    "src/status.c"
    "src/udp_nix.c"
    "src/udp_win.c"
    "src/crd.h"
    "src/bitfields.h"
    "src/command_interface.h"
    "src/md.h"
)

set(KATHERINE_HEADERS
    "include/katherine/acquisition.h"
    "include/katherine/config.h"
    "include/katherine/device.h"
    "include/katherine/global.h"
    "include/katherine/katherine.h"
    "include/katherine/px_config.h"
    "include/katherine/px.h"
    "include/katherine/status.h"
    "include/katherine/udp.h"
    "include/katherine/udp_nix.h"
    "include/katherine/udp_win.h"
)

add_library(katherine SHARED ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
set_target_properties(katherine PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(katherine
    PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# On Windows, link to ws2_32
if(WIN32 OR MINGW)
  target_link_libraries(katherine ws2_32)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/katherine.pc.in katherine.pc @ONLY)

install(TARGETS katherine LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${KATHERINE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherine)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherine.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

if(KATHERINE_BUILD_DOXYGEN)
    find_package(Doxygen "${KATHERINE_DOXYGEN_MINIMUM_VERSION}" REQUIRED)

    # Wrap the hand-maintained Doxyfile to redirect output into the build
    # tree; a distinct file name keeps in-source builds from clobbering it.
    set(KATHERINE_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")
    file(CONFIGURE OUTPUT katherine.doxyfile CONTENT [[
@INCLUDE = "@CMAKE_CURRENT_SOURCE_DIR@/Doxyfile"
OUTPUT_DIRECTORY = "@KATHERINE_DOXYGEN_OUTPUT_DIR@"
]] @ONLY)

    # Doxygen documents the same sources and headers that make up the
    # katherine target (INPUT = src, include in the Doxyfile), so reuse its
    # file lists as dependencies instead of globbing the directories. The
    # top-level README serves as the main page of the documentation.
    set(KATHERINE_DOXYGEN_INPUTS
        ${KATHERINE_SOURCES}
        ${KATHERINE_HEADERS}
        "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
    )

    # Snapshot the input list; only rewritten when the set of files changes.
    # Depending on it reruns doxygen also when a documented file is deleted
    # (timestamps of the surviving files alone would not catch that).
    file(CONFIGURE OUTPUT doxygen_inputs.txt CONTENT "${KATHERINE_DOXYGEN_INPUTS}")

    # Run from the source directory so the Doxyfile's relative INPUT paths
    # resolve. The stamp file makes the build incremental: doxygen reruns
    # only when a documented file or a Doxyfile changes.
    set(KATHERINE_DOXYGEN_STAMP "${CMAKE_CURRENT_BINARY_DIR}/docs.stamp")
    add_custom_command(
        OUTPUT "${KATHERINE_DOXYGEN_STAMP}"
        COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/katherine.doxyfile"
        COMMAND ${CMAKE_COMMAND} -E touch "${KATHERINE_DOXYGEN_STAMP}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS
            ${KATHERINE_DOXYGEN_INPUTS}
            Doxyfile
            DoxygenLayout.xml
            "${CMAKE_CURRENT_BINARY_DIR}/katherine.doxyfile"
            "${CMAKE_CURRENT_BINARY_DIR}/doxygen_inputs.txt"
        COMMENT "Building API documentation with Doxygen"
        VERBATIM
    )
    add_custom_target(docs ALL DEPENDS "${KATHERINE_DOXYGEN_STAMP}")

    install(DIRECTORY "${KATHERINE_DOXYGEN_OUTPUT_DIR}/html"
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif(KATHERINE_BUILD_DOXYGEN)

if(KATHERINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(KATHERINE_BUILD_EXAMPLES)
