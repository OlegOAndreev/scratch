project(memcpy-performance)
cmake_minimum_required(VERSION 2.8)

enable_language(C ASM)

# Architecture detection, best I could find: https://github.com/axr/solar-cmake/blob/master/TargetArch.cmake
set(ARCHDETECT_C_CODE "#if defined(__x86_64__) || defined(_M_X64)
#error cmake_ARCH x86_64
#elif defined(__aarch64__) || defined(_M_ARM64)
#error cmake_ARCH aarch64
#else
#error cmake_ARCH unsupported
#endif")
file(WRITE "${CMAKE_BINARY_DIR}/arch.c" "${ARCHDETECT_C_CODE}")
try_run(
  RUN_RESULT_UNUSED
  COMPILE_RESULT_UNUSED
  "${CMAKE_BINARY_DIR}"
  "${CMAKE_BINARY_DIR}/arch.c"
  COMPILE_OUTPUT_VARIABLE ARCH
)
string(REGEX MATCH "cmake_ARCH ([a-zA-Z0-9_]+)" ARCH "${ARCH}")
string(REPLACE "cmake_ARCH " "" ARCH "${ARCH}")

message("Detected arch: " ${ARCH})

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wshadow")
  if(${ARCH} MATCHES "x86_64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
    set(ADDITIONAL_SOURCES memcpy-impl-x86_64.S)
  elseif(${ARCH} MATCHES "aarch64")
    set(ADDITIONAL_SOURCES memcpy-impl-aarch64.S)
  endif()
endif()

add_executable(${PROJECT_NAME} 
  memcpy-performance.cpp
  memcpy-cpp-impl.cpp
  ${ADDITIONAL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE "..")
