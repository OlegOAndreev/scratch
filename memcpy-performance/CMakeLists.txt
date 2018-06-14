project(memcpy-performance)
cmake_minimum_required(VERSION 2.8)

enable_language(C ASM)

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow -mavx")
set(ADDITIONAL_SOURCES memcpy-impl.S)
endif()

add_executable(${PROJECT_NAME} 
  memcpy-performance.cpp
  memcpy-cpp-impl.cpp
  ${ADDITIONAL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE "..")
