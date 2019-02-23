add_library(katherinexx INTERFACE)
target_include_directories(katherinexx INTERFACE include)
target_link_libraries(katherinexx INTERFACE katherine)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(BUILD_EXAMPLES)
