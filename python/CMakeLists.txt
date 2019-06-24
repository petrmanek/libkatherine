find_package(Python3 COMPONENTS Interpreter Development)

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")

set(PY_OUTPUT   "${CMAKE_CURRENT_BINARY_DIR}/build/pykatherine")

set(PYKATHERINE_SOURCES
    "cacquisition.pxd"
    "cconfig.pxd"
    "cdevice.pxd"
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
