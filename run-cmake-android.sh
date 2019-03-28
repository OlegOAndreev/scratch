#!/bin/sh

# Script to run from build directory.

set -x

NDK_DIRECTORY=/Users/ooandreev/Library/Android/sdk/ndk-bundle
NDK_ARCH_ABI=arm64-v8a
#NDK_ARCH_ABI=armeabi-v7a
NDK_VERSION=24
NDK_TOOLCHAIN_VERSION=clang

# 1. Recent NDK have broken (?) ld invocation (see e.g.
#    https://github.com/flutter/flutter/issues/23458), fix by manually passing the LDFLAGS.
# 2. The linker expects to find libgcc in
#    toolchains/llvm/prebuilt/darwin-x86_64/lib64/clang/8.0.2/lib/linux/aarch64/ while it is
#    actually located in toolchains/llvm/prebuilt/darwin-x86_64/lib/gcc/aarch64-linux-android/4.9.x/
# 3. The linker can't find libatomic, manually add the required directory.
NDK_LD=`$NDK_DIRECTORY/ndk-which --abi $NDK_ARCH_ABI ld`

# LDFLAGS are the easiest way to pass linker flags to the CMake.
export LDFLAGS="-fuse-ld=$NDK_LD \
-L$NDK_DIRECTORY/toolchains/llvm/prebuilt/darwin-x86_64/lib/gcc/aarch64-linux-android/4.9.x \
-L$NDK_DIRECTORY/toolchains/llvm/prebuilt/darwin-x86_64/lib/gcc/arm-linux-android/4.9.x \
-L$NDK_DIRECTORY/toolchains/llvm/prebuilt/darwin-x86_64/aarch64-linux-android/lib64 \
-L$NDK_DIRECTORY/toolchains/llvm/prebuilt/darwin-x86_64/arm-linux-android/lib"

cmake -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_ANDROID_NDK=$NDK_DIRECTORY \
  -DCMAKE_ANDROID_ARCH_ABI=$NDK_ARCH_ABI \
  -DCMAKE_SYSTEM_VERSION=$NDK_VERSION \
  -DCMAKE_ANDROID_STL_TYPE=c++_static \
  -DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=$NDK_TOOLCHAIN_VERSION \
  $*
