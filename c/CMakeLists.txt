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
    "src/version.c"
    "src/crd.h"
    "src/bitfields.h"
    "src/command_interface.h"
    "src/msleep.h"
    "src/monoclock.h"
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

# The emulator is an optional part of the library: with the feature off,
# its sources are not compiled and its header is not installed.
if(KATHERINE_BUILD_EMULATOR)
    list(APPEND KATHERINE_SOURCES
        "src/emu/emulator.c"
        "src/emu/emu_stream.c"
        "src/emu/emu.h"
    )
    list(APPEND KATHERINE_HEADERS
        "include/katherine/emulator.h"
    )
endif()

# Generated from version.h.in with the values from the project() call
# above, so the version macros can never drift from CMakeLists.txt.
set(KATHERINE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
set(KATHERINE_VERSION_HEADER "${KATHERINE_GENERATED_INCLUDE_DIR}/katherine/version.h")
configure_file(include/katherine/version.h.in "${KATHERINE_VERSION_HEADER}" @ONLY)

# The project-wide documentation build in docs/ documents these files.
set(KATHERINE_DOCUMENTED_FILES ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
list(TRANSFORM KATHERINE_DOCUMENTED_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
# version.h is generated (its .in template is not valid C due to the
# @VAR@ substitution placeholders), so the generated copy is documented
# instead, from its location in the build tree.
list(APPEND KATHERINE_DOCUMENTED_FILES "${KATHERINE_VERSION_HEADER}")
set(KATHERINE_DOCUMENTED_FILES "${KATHERINE_DOCUMENTED_FILES}" PARENT_SCOPE)

add_library(katherine SHARED ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
add_library(katherine::katherine ALIAS katherine)

# The private header surface under c/src, for in-tree tests and benchmarks:
# linking this interface target grants the internal include path without
# installing or exporting anything. Out-of-tree code must not use it.
add_library(katherine_private INTERFACE)
target_include_directories(katherine_private INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(katherine::private ALIAS katherine_private)

# The implementation needs C11; the public headers only need C99, so this
# is not a usage requirement.
target_compile_features(katherine PRIVATE c_std_11)
set_target_properties(katherine PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(katherine
    PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${KATHERINE_GENERATED_INCLUDE_DIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# On Windows, link to ws2_32
if(WIN32 OR MINGW)
  target_link_libraries(katherine PRIVATE ws2_32)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/katherine.pc.in katherine.pc @ONLY)

# On Windows the DLL is a RUNTIME artifact and the import library an ARCHIVE
# artifact; elsewhere only the LIBRARY destination applies.
install(TARGETS katherine
        EXPORT katherine-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${KATHERINE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherine)
install(FILES ${KATHERINE_VERSION_HEADER} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherine)
# The .pc file records the architecture-dependent libdir, so it belongs in
# the libdir pkgconfig directory rather than the shared-data one.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherine.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(KATHERINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KATHERINE_BUILD_TESTS)
    include(KatherineTests)
    add_subdirectory(tests)
endif()

if(KATHERINE_BUILD_BENCHMARKS)
    include(KatherineBenchmarks)
    add_subdirectory(benchmarks)
endif()
