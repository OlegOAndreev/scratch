project(test-pool)
cmake_minimum_required(VERSION 2.8)

add_executable(${PROJECT_NAME}
    test-pool.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ..)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -Wshadow")
#  target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
#  target_link_libraries(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
#  target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=thread)
#  target_link_libraries(${PROJECT_NAME} PRIVATE -fsanitize=thread)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  target_link_libraries(${PROJECT_NAME} rt)
endif()
