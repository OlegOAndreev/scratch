cmake_minimum_required(VERSION 3.14)

project(scratch LANGUAGES C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wshadow)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-std=c++17>")
    # add_compile_options(-fsanitize=address,undefined)
    # add_link_options(-fsanitize=address,undefined)
    # add_compile_options(-fsanitize=thread)
    # add_link_options(-fsanitize=thread)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    add_compile_options(-pthread)
    add_link_options(-pthread)
    link_libraries(rt)
endif()

if(UNIX)
    include_directories(/usr/local/include)
    link_directories(/usr/local/lib)
endif()

if(NOT ANDROID)
    # SDL2 (https://www.libsdl.org) must be installed (either in /usr/local or locally)
    find_file(SDL_H SDL2/SDL.h)
    if(NOT SDL_H)
        message(FATAL_ERROR "SDL2/SDL.h not found, compile and install SDL2")
    endif()
    find_library(SDL_LIB SDL2)
    if(NOT SDL_LIB)
        message(FATAL_ERROR "SDL2 library not found, compile and install SDL2")
    endif()

    # libsoundio (http://libsound.io) must be installed (either in /usr/local or locally)
    find_file(SOUNDIO_H soundio/soundio.h)
    if(NOT SOUNDIO_H)
        message(FATAL_ERROR "soundio/soundio.h not found, compile and install libsoundio")
    endif()
    find_library(SOUNDIO_LIB soundio)
    if(NOT SOUNDIO_LIB)
        message(FATAL_ERROR "soundio library not found, compile and install soundio")
    endif()
endif()

include_directories(.)

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
    COMPILE_OUTPUT_VARIABLE ARCH)
string(REGEX MATCH "cmake_ARCH ([a-zA-Z0-9_]+)" ARCH "${ARCH}")
string(REPLACE "cmake_ARCH " "" ARCH "${ARCH}")

message("Detected arch: " ${ARCH})


add_executable(atomic-add-performance atomic-add-performance/atomic-add-performance.cpp)


add_executable(audio-stretch
    audio-stretch/audio-stretch.cpp
    3rdparty/kiss_fft/kiss_fft.c
    3rdparty/kiss_fft/kiss_fftr.c)

if(NOT ANDROID)
    add_executable(audio-test audio-test/audio-test.cpp)
    target_link_libraries(audio-test ${SDL_LIB} ${SOUNDIO_LIB})
endif()


add_executable(common-test common-test/common-test.cpp)
add_executable(common-bench common-test/common-bench.cpp)


add_executable(external-sort external-sort/external-sort.cpp)
target_include_directories(external-sort PRIVATE sort)


if(NOT MSVC)
    if(${ARCH} MATCHES "x86_64")
        set(FIBER_ADDITIONAL_SOURCES
            3rdparty/marl/osfiber_asm_x64.S
            3rdparty/marl/osfiber_x64.c
            fiber/marl-fiber.cpp)
    elseif(${ARCH} MATCHES "aarch64")
        set(FIBER_ADDITIONAL_SOURCES
            3rdparty/marl/osfiber_asm_aarch64.S
            3rdparty/marl/osfiber_aarch64.c
            fiber/marl-fiber.cpp)
    endif()
endif()
add_executable(test-fiber fiber/os-fiber.cpp fiber/test-fiber.cpp ${FIBER_ADDITIONAL_SOURCES})


if(NOT ANDROID)
    add_executable(gamepad-id gamepad-id/gamepad-id.cpp)
    target_link_libraries(gamepad-id ${SDL_LIB})
endif()


if(NOT MSVC)
    if(${ARCH} MATCHES "x86_64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
        set(MEMCPY_PERFORMANCE_ADDITIONAL_SOURCES memcpy-performance/memcpy-impl-x86_64.S)
    elseif(${ARCH} MATCHES "aarch64")
        set(MEMCPY_PERFORMANCE_ADDITIONAL_SOURCES memcpy-performance/memcpy-impl-aarch64.S)
    endif()
endif()
add_executable(memcpy-performance
    memcpy-performance/memcpy-performance.cpp
    memcpy-performance/memcpy-cpp-impl.cpp
    ${MEMCPY_PERFORMANCE_ADDITIONAL_SOURCES})


add_executable(n-queens n-queens/n-queens.cpp)


if(APPLE)
    find_library(APPLICATION_SERVICES_LIB ApplicationServices)
    if(NOT APPLICATION_SERVICES_LIB)
        message(FATAL_ERROR "ApplicationServices not found")
    endif()

    find_library(IOKIT_LIB IOKit)
    if(NOT IOKIT_LIB)
        message(FATAL_ERROR "IOKit not found")
    endif()

    add_executable(osx-mouse-test osx-mouse-test/osx-mouse-test.cpp)
    target_link_libraries(osx-mouse-test ${APPLICATION_SERVICES_LIB})
    target_link_libraries(osx-mouse-test ${IOKIT_LIB})
endif()


add_executable(test-sort sort/test-sort.cpp)


add_executable(test-sync
    sync/test-countwaiter.cpp
    sync/test-queues.cpp
    sync/test-semaphores.cpp
    sync/test-sync.cpp)


add_executable(test-pool
    threadpool/test-pool.cpp
    threadpool/testfixedfunction.cpp
    threadpool/testpools.cpp)


add_executable(timer-resolution timer-resolution/timer-resolution.cpp)


add_executable(vcall-performance vcall-performance/vcall-performance.cpp)
