project(audio-stretch)
cmake_minimum_required(VERSION 2.8)

add_executable(${PROJECT_NAME}
    audio-stretch.cpp
    kiss_fft/kiss_fft.c
    kiss_fft/kiss_fftr.c)

target_include_directories(${PROJECT_NAME} PRIVATE ..)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
#  target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
#  target_link_libraries(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
  target_link_libraries(${PROJECT_NAME}
      rt)
endif()
