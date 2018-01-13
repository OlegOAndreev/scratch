project(audio-stretch)
cmake_minimum_required(VERSION 2.8)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
  if (APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -glldb")
  endif()
endif()

add_executable(${PROJECT_NAME} audio-stretch.cpp)

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} rt)
endif()
