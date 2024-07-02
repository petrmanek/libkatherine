add_library(katherinexx INTERFACE)
target_include_directories(katherinexx INTERFACE include)
target_link_libraries(katherinexx INTERFACE katherine)

file(GLOB headers "include/katherinexx/*")
target_sources(
  katherinexx
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES ${headers}
)

INSTALL(TARGETS
  katherinexx
  FILE_SET public_headers
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(BUILD_EXAMPLES)
