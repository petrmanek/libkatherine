add_library(katherinexx INTERFACE)
target_include_directories(katherinexx INTERFACE include)
target_link_libraries(katherinexx INTERFACE katherine)

add_subdirectory(examples)
