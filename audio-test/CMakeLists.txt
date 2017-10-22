project(audio-test)
cmake_minimum_required(VERSION 2.8)
include(CheckIncludeFiles)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fsanitize=address")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  include_directories(/usr/local/include)
endif()

# SDL2 (https://www.libsdl.org) must be installed (either in /usr/local or locally)
check_include_files("SDL2/SDL.h" HAVE_SDL_H)
if(NOT HAVE_SDL_H)
  message(FATAL_ERROR "SDL2/SDL.h not found, compile and install SDL2")
endif()
list(APPEND LIBRARIES SDL2)

# libsoundio (http://libsound.io) must be installed (either in /usr/local or locally)
check_include_files("soundio/soundio.h" HAVE_SOUNDIO_H)
if(NOT HAVE_SOUNDIO_H)
  message(FATAL_ERROR "soundio/soundio.h not found, compile and install libsoundio")
endif()
list(APPEND LIBRARIES soundio)

add_executable(${PROJECT_NAME} audio-test.cpp)

target_link_libraries(${PROJECT_NAME} ${LIBRARIES})
