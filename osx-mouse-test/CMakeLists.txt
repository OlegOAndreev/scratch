project(osx-mouse-test)
cmake_minimum_required(VERSION 2.8)

if(NOT APPLE)
message(FATAL_ERROR "Only macOS is supported")
endif()

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
endif()

find_library(APPLICATION_SERVICES_LIB ApplicationServices)
if (NOT APPLICATION_SERVICES_LIB)
message(FATAL_ERROR "ApplicationServices not found")
endif()

find_library(IOKIT_LIB IOKit)
if (NOT IOKIT_LIB)
message(FATAL_ERROR "IOKit not found")
endif()

add_executable(${PROJECT_NAME} osx-mouse-test.cpp)
target_link_libraries(${PROJECT_NAME} ${APPLICATION_SERVICES_LIB})
target_link_libraries(${PROJECT_NAME} ${IOKIT_LIB})
