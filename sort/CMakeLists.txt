project(test-sort)
cmake_minimum_required(VERSION 2.8.12)

add_executable(${PROJECT_NAME}
  test-sort.cpp
  ${ADDITIONAL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ..)

if(NOT MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE -std=c++11 -Wall -Wextra -Wshadow)
  # target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
  # target_link_libraries(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()
