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
# The binary directory is stripped as well as the source one, because the
# generated version.h is documented from where it is generated. Without it an
# in-tree build names a "build" directory in the output and an out-of-tree one
# embeds the absolute path of whoever generated the documentation.
STRIP_FROM_PATH = "@PROJECT_SOURCE_DIR@" "@PROJECT_BINARY_DIR@"
STRIP_FROM_INC_PATH = "@PROJECT_SOURCE_DIR@/c/include" "@PROJECT_SOURCE_DIR@/cxx/include"
]] @ONLY)

# The manual pages come from a second run, over the C sources alone.
#
# man3 is one flat namespace, and MAN_LINKS -- which is what makes `man 3
# katherine_device_init` resolve at all -- adds a page per documented entity,
# named after that entity alone. Documenting the C++ wrapper in the same run
# therefore lands katherine::udp::recv in man3 as recv.3, katherine::emu::log
# as log.3, and katherine::base_acquisition::abort and ::read as abort.3 and
# read.3, each shadowing the libc page of the same name. The class pages
# collide outright: katherine::device is written as katherine_device.3, the
# same file as the Device group, and whichever doxygen emits second wins --
# which silently pointed every katherine_device_* page at the C++ class.
#
# Restricting this run to c/ removes both problems at once, and a C library's
# manual has no business describing a C++ class hierarchy anyway. The HTML
# still documents everything, from the run above.
#
# The result is readable straight out of the build tree, without installing:
#
#     man -M <build>/docs/man 3 katherine_device_init
#
# -M names the root of a man hierarchy, which is the directory holding man3,
# so the `.so man3/...` redirects resolve exactly as they will once installed.
set(KATHERINE_DOXYGEN_MAN_INPUTS ${KATHERINE_DOCUMENTED_FILES})
list(JOIN KATHERINE_DOXYGEN_MAN_INPUTS "\" \"" KATHERINE_DOXYGEN_MAN_INPUT_LINE)
set(KATHERINE_DOXYGEN_MAN_INPUT_LINE "\"${KATHERINE_DOXYGEN_MAN_INPUT_LINE}\"")
file(CONFIGURE OUTPUT katherine-man.doxyfile CONTENT [[
@INCLUDE = "@CMAKE_CURRENT_SOURCE_DIR@/Doxyfile"
OUTPUT_DIRECTORY = "@KATHERINE_DOXYGEN_OUTPUT_DIR@"
PROJECT_NUMBER = @PROJECT_VERSION@
INPUT = @KATHERINE_DOXYGEN_MAN_INPUT_LINE@
STRIP_FROM_PATH = "@PROJECT_SOURCE_DIR@" "@PROJECT_BINARY_DIR@"
STRIP_FROM_INC_PATH = "@PROJECT_SOURCE_DIR@/c/include"
GENERATE_HTML = NO
HAVE_DOT = NO
GENERATE_MAN = YES
MAN_LINKS = YES
# This run has nothing to report that the run above has not already
# reported, so its warnings go to a file rather than to the build output --
# where they would double every count taken from the log, and where the
# restricted input would add a complaint about each cross-reference into the
# C++ wrapper it can no longer resolve. WARNINGS = NO alone does not do it:
# WARN_IF_UNDOCUMENTED emits whatever that tag says.
QUIET = YES
WARNINGS = NO
WARN_IF_UNDOCUMENTED = NO
WARN_IF_DOC_ERROR = NO
WARN_LOGFILE = "@KATHERINE_DOXYGEN_OUTPUT_DIR@/doxygen-man.log"
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
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${KATHERINE_DOXYGEN_OUTPUT_DIR}/man"
    COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/katherine-man.doxyfile"
    COMMAND ${CMAKE_COMMAND} -DMAN_DIR=${KATHERINE_DOXYGEN_OUTPUT_DIR}/man/man3
            -P "${CMAKE_CURRENT_SOURCE_DIR}/prune_man.cmake"
    COMMAND ${CMAKE_COMMAND} -E touch "${KATHERINE_DOXYGEN_STAMP}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        ${KATHERINE_DOXYGEN_INPUTS}
        Doxyfile
        DoxygenLayout.xml
        prune_man.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/katherine.doxyfile"
        "${CMAKE_CURRENT_BINARY_DIR}/katherine-man.doxyfile"
        "${CMAKE_CURRENT_BINARY_DIR}/doxygen_inputs.txt"
    COMMENT "Building API documentation with Doxygen"
    VERBATIM
)
add_custom_target(docs ALL DEPENDS "${KATHERINE_DOXYGEN_STAMP}")

install(DIRECTORY "${KATHERINE_DOXYGEN_OUTPUT_DIR}/html"
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Manual pages go to POSIX systems only; Windows has nothing to read them.
# The directory is installed as it stands: prune_man.cmake has already
# reduced it to the pages that belong in a man hierarchy.
if(NOT WIN32)
    install(DIRECTORY "${KATHERINE_DOXYGEN_OUTPUT_DIR}/man/man3"
            DESTINATION ${CMAKE_INSTALL_MANDIR})
endif()
