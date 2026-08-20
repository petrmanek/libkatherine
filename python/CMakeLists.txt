# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

find_package(Python3 "${KATHERINE_PYTHON_MINIMUM_VERSION}" REQUIRED
             COMPONENTS Interpreter Development)

# setup.py imports these modules at build time; verify them now so a broken
# environment fails the configuration rather than the first build.
list(JOIN KATHERINE_PYTHON_REQUIRED_MODULES ", " KATHERINE_PYTHON_IMPORTS)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import ${KATHERINE_PYTHON_IMPORTS}"
                RESULT_VARIABLE KATHERINE_PYTHON_IMPORT_RESULT
                OUTPUT_QUIET ERROR_QUIET)
if(NOT KATHERINE_PYTHON_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR "KATHERINE_BUILD_PYTHON requires the Python modules "
                        "${KATHERINE_PYTHON_IMPORTS} to be importable by "
                        "${Python3_EXECUTABLE}")
endif()

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")

set(PY_OUTPUT   "${CMAKE_CURRENT_BINARY_DIR}/build/pykatherine")

set(PYKATHERINE_SOURCES
    "cacquisition.pxd"
    "cconfig.pxd"
    "cdevice.pxd"
    "cpx_config.pxd"
    "cstatus.pxd"
    "katherine.pyx"
)

configure_file(${SETUP_PY_IN} ${SETUP_PY})

add_custom_command(OUTPUT "${PY_OUTPUT}"
                   COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} build_ext
                   COMMAND ${CMAKE_COMMAND} -E touch ${PY_OUTPUT}
                   COMMENT "Generating Python bindings"
               )

add_custom_target(pykatherine
                  ALL
                  DEPENDS ${PY_OUTPUT} katherine
                  SOURCES ${PYKATHERINE_SOURCES}
                )

install(CODE "execute_process(COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} install)")
