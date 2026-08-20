# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

find_package(Doxygen "${KATHERINE_DOXYGEN_MINIMUM_VERSION}" REQUIRED)

# Render dependency graphs when Graphviz is available (found by
# FindDoxygen as the optional dot component).
if(TARGET Doxygen::dot)
    set(KATHERINE_DOXYGEN_HAVE_DOT YES)
    get_target_property(KATHERINE_DOXYGEN_DOT Doxygen::dot IMPORTED_LOCATION)
    get_filename_component(KATHERINE_DOXYGEN_DOT_DIR "${KATHERINE_DOXYGEN_DOT}" DIRECTORY)
else()
    set(KATHERINE_DOXYGEN_HAVE_DOT NO)
    set(KATHERINE_DOXYGEN_DOT_DIR "")
endif()

# Doxygen documents the same files that make up the library targets: the c/
# and cxx/ directories export their file lists, so the documented set cannot
# drift from the built set, and the C++ wrapper is only documented when it is
# part of the build. The top-level README serves as the main page.
set(KATHERINE_DOXYGEN_INPUTS
    ${KATHERINE_DOCUMENTED_FILES}
    "${PROJECT_SOURCE_DIR}/README.md"
)
if(KATHERINE_BUILD_CXX)
    list(APPEND KATHERINE_DOXYGEN_INPUTS ${KATHERINEXX_DOCUMENTED_FILES})
endif()
list(JOIN KATHERINE_DOXYGEN_INPUTS "\" \"" KATHERINE_DOXYGEN_INPUT_LINE)
set(KATHERINE_DOXYGEN_INPUT_LINE "\"${KATHERINE_DOXYGEN_INPUT_LINE}\"")

# Wrap the hand-maintained Doxyfile to redirect output into the build tree
# and to supply the build-dependent settings; a distinct file name keeps
# in-source builds from clobbering it.
set(KATHERINE_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
file(CONFIGURE OUTPUT katherine.doxyfile CONTENT [[
@INCLUDE = "@CMAKE_CURRENT_SOURCE_DIR@/Doxyfile"
OUTPUT_DIRECTORY = "@KATHERINE_DOXYGEN_OUTPUT_DIR@"
PROJECT_NUMBER = @PROJECT_VERSION@
HAVE_DOT = @KATHERINE_DOXYGEN_HAVE_DOT@
DOT_PATH = "@KATHERINE_DOXYGEN_DOT_DIR@"
INPUT = @KATHERINE_DOXYGEN_INPUT_LINE@
USE_MDFILE_AS_MAINPAGE = "@PROJECT_SOURCE_DIR@/README.md"
STRIP_FROM_PATH = "@PROJECT_SOURCE_DIR@"
STRIP_FROM_INC_PATH = "@PROJECT_SOURCE_DIR@/c/include" "@PROJECT_SOURCE_DIR@/cxx/include"
]] @ONLY)

# Snapshot the input list; only rewritten when the set of files changes.
# Depending on it reruns doxygen also when a documented file is deleted
# (timestamps of the surviving files alone would not catch that).
file(CONFIGURE OUTPUT doxygen_inputs.txt CONTENT "${KATHERINE_DOXYGEN_INPUTS}")

# Run from this directory so the Doxyfile's relative LAYOUT_FILE resolves.
# The stamp file makes the build incremental: doxygen reruns only when a
# documented file or a Doxyfile changes.
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
