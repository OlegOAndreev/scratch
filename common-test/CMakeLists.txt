project(common)
cmake_minimum_required(VERSION 2.8)

add_executable(common-test
    common-test.cpp)
add_executable(common-bench
    common-bench.cpp)

target_include_directories(common-test PRIVATE ..)
target_include_directories(common-bench PRIVATE ..)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Wextra -Wshadow")
  target_compile_options(common-test PRIVATE -fsanitize=address,undefined)
  target_link_libraries(common-test PRIVATE -fsanitize=address,undefined)
#  target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=thread)
#  target_link_libraries(${PROJECT_NAME} PRIVATE -fsanitize=thread)
#  target_compile_options(common-bench PRIVATE -fsanitize=address,undefined)
#  target_link_libraries(common-bench PRIVATE -fsanitize=address,undefined)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  target_link_libraries(common-test rt)
  target_link_libraries(common-bench rt)
endif()
