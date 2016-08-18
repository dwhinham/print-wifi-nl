cmake_minimum_required(VERSION 2.8)
project(print-wifi-nl)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(NL REQUIRED)

add_executable(print-wifi-nl print-wifi-nl.c)
include_directories(${NL_INCLUDE_DIRS})
target_link_libraries(print-wifi-nl ${NL_LIBRARIES})
install(TARGETS print-wifi-nl DESTINATION bin)
