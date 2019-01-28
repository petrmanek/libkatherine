add_executable(kfindxx kfindxx.cpp address_source.cpp address_source.hpp)
target_link_libraries(kfindxx katherinexx)

add_executable(krunxx krunxx.cpp)
target_link_libraries(krunxx katherinexx)
