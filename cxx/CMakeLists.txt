set(KATHERINEXX_HEADERS
    "include/katherinexx/acquisition.hpp"
    "include/katherinexx/config.hpp"
    "include/katherinexx/device.hpp"
    "include/katherinexx/error.hpp"
    "include/katherinexx/katherinexx.hpp"
    "include/katherinexx/px_config.hpp"
)

add_library(katherinexx INTERFACE)
target_link_libraries(katherinexx INTERFACE katherine)
target_include_directories(katherinexx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/katherinexx.pc.in katherinexx.pc @ONLY)

install(FILES ${KATHERINEXX_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherinexx)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherinexx.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(BUILD_EXAMPLES)
