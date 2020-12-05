#!/bin/sh

# Script to run from build directory.

set -x

NDK_ROOT=/Users/ooandreev/Library/Android/sdk/ndk
NDK=`find $NDK_ROOT -type d -depth 1`
NDK_ARCH_ABI=arm64-v8a

cmake \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=$NDK_ARCH_ABI \
  $*
