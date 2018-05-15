project(audio-stretch)
cmake_minimum_required(VERSION 2.8)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
  if (APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -glldb")
  endif()
endif()

add_definitions(-DHAVE_FFTW3)

add_executable(${PROJECT_NAME} audio-stretch.cpp kiss_fft/kiss_fft.c kiss_fft/kiss_fftr.c)

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} rt)
endif()
