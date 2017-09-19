project(gamepad-id)
cmake_minimum_required(VERSION 2.8)

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -ggdb -Wall -Wextra -Wshadow")
endif()

include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)

include_directories(${SDL2_INCLUDE_DIRS})
link_directories(${SDL2_LIBRARY_DIRS})

add_executable(${PROJECT_NAME} gamepad-id.cpp)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
