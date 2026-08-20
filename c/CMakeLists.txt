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

# The project-wide documentation build in docs/ documents these files.
set(KATHERINE_DOCUMENTED_FILES ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
list(TRANSFORM KATHERINE_DOCUMENTED_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(KATHERINE_DOCUMENTED_FILES "${KATHERINE_DOCUMENTED_FILES}" PARENT_SCOPE)

add_library(katherine SHARED ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
add_library(katherine::katherine ALIAS katherine)
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

# On Windows the DLL is a RUNTIME artifact and the import library an ARCHIVE
# artifact; elsewhere only the LIBRARY destination applies.
install(TARGETS katherine
        EXPORT katherine-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${KATHERINE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherine)
# The .pc file records the architecture-dependent libdir, so it belongs in
# the libdir pkgconfig directory rather than the shared-data one.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherine.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(KATHERINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(KATHERINE_BUILD_EXAMPLES)
