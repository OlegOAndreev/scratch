project(vcall-performance)
cmake_minimum_required(VERSION 2.8)

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
endif()

add_executable(${PROJECT_NAME} vcall-performance.cpp)
