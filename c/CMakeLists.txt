set(KATHERINE_SOURCES
    "src/acquisition.c"
    "src/bmc.c"
    "src/config.c"
    "src/device.c"
    "src/status.c"
    "src/udp.c"
)

set(KATHERINE_HEADERS
    "include/katherine/acquisition.h"
    "include/katherine/bmc.h"
    "include/katherine/command_interface.h"
    "include/katherine/config.h"
    "include/katherine/device.h"
    "include/katherine/katherine.h"
    "include/katherine/status.h"
    "include/katherine/udp.h"
)

add_library(katherine ${KATHERINE_SOURCES} ${KATHERINE_HEADERS})
target_include_directories(katherine PUBLIC include)

add_subdirectory(examples)