# CMake toolchain file for cross-compiling to aarch64 (ARM64)
# For use with EmulationStation-fcamod on ArkOS
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake \
#         -DTOOLCHAIN_ROOT=/opt/toolchains/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu \
#         -DTARGET_SYSROOT=/path/to/sysroot \
#         -DGLES=ON ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Toolchain path (can be overridden via command line)
# Using GCC 7.5.0 for better compatibility (chroot uses GCC 8.3.0)
if(NOT DEFINED TOOLCHAIN_ROOT)
    set(TOOLCHAIN_ROOT "/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu")
endif()

# Sysroot path - use sysroot-all by default for cross-compilation
if(NOT DEFINED TARGET_SYSROOT)
    set(TARGET_SYSROOT "/home/lcdyk/cross-build/sysroot-all")
endif()

# Specify the cross compiler (Linaro uses aarch64-linux-gnu prefix)
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-linux-gnu-g++")

# Specify the sysroot (CMake will automatically add --sysroot to compiler/linker flags)
set(CMAKE_SYSROOT "${TARGET_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${TARGET_SYSROOT}")

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Pkg-config settings for cross compilation
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${TARGET_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${TARGET_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${TARGET_SYSROOT}/lib/aarch64-linux-gnu/pkgconfig:${TARGET_SYSROOT}/usr/lib/pkgconfig:${TARGET_SYSROOT}/usr/share/pkgconfig")

message(STATUS "========================================")
message(STATUS "Cross-compiling for aarch64")
message(STATUS "  Toolchain: ${TOOLCHAIN_ROOT}")
message(STATUS "  Sysroot: ${TARGET_SYSROOT}")
message(STATUS "========================================")