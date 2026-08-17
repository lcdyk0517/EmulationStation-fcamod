set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{SDK_DIR})
    set(SDK_DIR "$ENV{SDK_DIR}")
else()
    set(SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../rk3326-sdk-proper")
endif()

set(TARGET_SYSROOT "${SDK_DIR}/gcc-aarch64/aarch64-linux-gnu/libc")
set(GCC_LIB_DIR "${TARGET_SYSROOT}/lib/gcc/aarch64-linux-gnu/8")

set(CMAKE_C_COMPILER "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-g++")
set(CMAKE_AR "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-ar")
set(CMAKE_RANLIB "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-ranlib")
set(CMAKE_STRIP "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-strip")

set(CMAKE_SYSROOT "${TARGET_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${TARGET_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${TARGET_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${TARGET_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${TARGET_SYSROOT}/lib/aarch64-linux-gnu/pkgconfig:${TARGET_SYSROOT}/usr/lib/pkgconfig:${TARGET_SYSROOT}/usr/share/pkgconfig")
set(PKG_CONFIG_EXECUTABLE "${SDK_DIR}/gcc-aarch64/bin/aarch64-linux-gnu-pkg-config")

set(CMAKE_EXE_LINKER_FLAGS "-L${TARGET_SYSROOT}/usr/lib/aarch64-linux-gnu -L${TARGET_SYSROOT}/lib/aarch64-linux-gnu -L${GCC_LIB_DIR}")
set(CMAKE_SHARED_LINKER_FLAGS "-L${TARGET_SYSROOT}/usr/lib/aarch64-linux-gnu -L${TARGET_SYSROOT}/lib/aarch64-linux-gnu -L${GCC_LIB_DIR}")

message(STATUS "========================================")
message(STATUS "Cross-compiling for aarch64")
message(STATUS "  SDK: ${SDK_DIR}")
message(STATUS "  Sysroot: ${TARGET_SYSROOT}")
message(STATUS "========================================")
