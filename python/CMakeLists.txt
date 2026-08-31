# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

find_package(Python3 "${KATHERINE_PYTHON_MINIMUM_VERSION}" REQUIRED
             COMPONENTS Interpreter Development.Module)

# The bindings are transpiled to C by the cython compiler; verify it now so a
# broken environment fails the configuration rather than the first build.
execute_process(COMMAND "${Python3_EXECUTABLE}" -m cython --version
                RESULT_VARIABLE KATHERINE_CYTHON_RESULT
                OUTPUT_VARIABLE KATHERINE_CYTHON_OUTPUT
                ERROR_VARIABLE KATHERINE_CYTHON_OUTPUT)
if(NOT KATHERINE_CYTHON_RESULT EQUAL 0
   OR NOT KATHERINE_CYTHON_OUTPUT MATCHES "Cython version ([0-9.]+)"
   OR CMAKE_MATCH_1 VERSION_LESS KATHERINE_CYTHON_MINIMUM_VERSION)
    message(FATAL_ERROR "KATHERINE_BUILD_PYTHON requires Cython "
                        "${KATHERINE_CYTHON_MINIMUM_VERSION} or newer to be "
                        "usable by ${Python3_EXECUTABLE}")
endif()

set(PYKATHERINE_PYX "${CMAKE_CURRENT_SOURCE_DIR}/katherine.pyx")
set(PYKATHERINE_C   "${CMAKE_CURRENT_BINARY_DIR}/katherine.c")

set(PYKATHERINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/cacquisition.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cconfig.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cdevice.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cerror.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cpx.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cpx_config.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cstatus.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/ctoa.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cudp.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/cversion.pxd"
    "${PYKATHERINE_PYX}"
)

add_custom_command(OUTPUT "${PYKATHERINE_C}"
                   COMMAND "${Python3_EXECUTABLE}" -m cython -3
                           -I "${CMAKE_CURRENT_SOURCE_DIR}"
                           -o "${PYKATHERINE_C}"
                           "${PYKATHERINE_PYX}"
                   DEPENDS ${PYKATHERINE_SOURCES}
                   COMMENT "Cythonizing katherine.pyx"
                   VERBATIM)

# The extension module is importable as 'katherine'; the target keeps the
# historical name because the C library already claims 'katherine'.
Python3_add_library(pykatherine MODULE WITH_SOABI "${PYKATHERINE_C}")
set_target_properties(pykatherine PROPERTIES
    OUTPUT_NAME katherine
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
)
target_link_libraries(pykatherine PRIVATE katherine)

# Default destination of the Python module. An install into the default
# system prefix goes straight to the interpreter's site-packages, so that
# 'import katherine' works with no further setup. An install into a custom
# prefix keeps the module under that prefix instead (mirroring the
# interpreter's own site-packages layout), like every other artifact.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(KATHERINE_PYTHON_DEFAULT_INSTALL_DIR "${Python3_SITEARCH}")
else()
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import sys; print(sys.prefix)"
                    OUTPUT_VARIABLE KATHERINE_PYTHON_SYS_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(RELATIVE_PATH KATHERINE_PYTHON_DEFAULT_INSTALL_DIR
         "${KATHERINE_PYTHON_SYS_PREFIX}" "${Python3_SITEARCH}")
endif()

# Deliberately STRING, not PATH: cmake absolutizes a relative -D value of a
# PATH-typed cache entry against the shell's working directory before the
# project sees it, which would silently break the anchored-at-the-prefix
# contract promised below.
set(KATHERINE_PYTHON_INSTALL_DIR "${KATHERINE_PYTHON_DEFAULT_INSTALL_DIR}" CACHE STRING
    "Installation directory of the Python module (a relative path is anchored at the install prefix)")

# With the default prefix, the destination is the interpreter's absolute
# site-packages directory, which -Wdev flags as install-absolute-destination.
# That is deliberate: the module must land on sys.path to be importable with
# no further setup, and DESTDIR staging relocates absolute destinations on
# *nix regardless. Relocatable installs (custom prefixes, packaging) use a
# prefix-relative destination and are not affected.
install(TARGETS pykatherine LIBRARY DESTINATION "${KATHERINE_PYTHON_INSTALL_DIR}")

if(WIN32)
    # Python 3.8+ does not search PATH for the dependencies of an extension
    # module, and rpath does not exist on Windows; ship the C library next to
    # the module so that it is importable with no further setup.
    install(FILES $<TARGET_FILE:katherine> DESTINATION "${KATHERINE_PYTHON_INSTALL_DIR}")
endif()

# Smoke test against ksim, the standalone daemon hosting the protocol
# emulator (tools/ksim), driven through the built extension module. Guarded
# the same way c/tests guards its own e2e test against the same daemon:
# KATHERINE_BUILD_TESTS is what turns on test registration at all, and
# KATHERINE_BUILD_EMULATOR is what brings the ksim target into the build (the
# daemon the script spawns). No add_dependencies() on ksim is needed here,
# unlike in c/tests/CMakeLists.txt: that dependency orders the *build* of one
# target against another for a parallel build, but this test is not a build
# target, only a CTest entry that runs the already-built ksim and pykatherine
# after the build step has completed.
#
# The fixed ports 1555/1556 are a global resource, so this test must not run
# beside anything else that binds them (RUN_SERIAL); the timeout is a
# generous multiple of the couple of seconds a healthy run takes.
# SKIP_RETURN_CODE lets the script report at run time that its environment
# cannot host the daemon (see python/tests/smoke.py) rather than failing.
if(KATHERINE_BUILD_TESTS AND KATHERINE_BUILD_EMULATOR)
    add_test(NAME test_python_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.py"
                     "$<TARGET_FILE:ksim>" "$<TARGET_FILE_DIR:pykatherine>")
    set_tests_properties(test_python_smoke PROPERTIES
        LABELS "e2e;python"
        RUN_SERIAL TRUE
        TIMEOUT 120
        SKIP_RETURN_CODE 77)
endif()
