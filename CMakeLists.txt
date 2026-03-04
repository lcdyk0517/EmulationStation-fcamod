cmake_minimum_required(VERSION 2.8)

option(GLES "Set to ON if targeting Embedded OpenGL" ${GLES})
option(GL "Set to ON if targeting Desktop OpenGL" ${GL})
option(RPI "Set to ON to enable the Raspberry PI video player (omxplayer)" ${RPI})
option(CEC "Set to ON to enable CEC" ${CEC})

project(emulationstation-all)

#-------------------------------------------------------------------------------
#add local find scripts to CMAKE path
LIST(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Packages
)

#-------------------------------------------------------------------------------
# Cross-compilation support: Print info
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "Sysroot: ${CMAKE_SYSROOT}")
    message(STATUS "Find root paths: ${CMAKE_FIND_ROOT_PATH}")
endif()

#-------------------------------------------------------------------------------
#set up OpenGL system variable
if(GLES)
    set(GLSystem "Embedded OpenGL" CACHE STRING "The OpenGL system to be used")
elseif(GL)
    set(GLSystem "Desktop OpenGL" CACHE STRING "The OpenGL system to be used")
#-------------------------------------------------------------------------------
#check if we're running on Raspberry Pi
elseif(EXISTS "${CMAKE_FIND_ROOT_PATH}/opt/vc/include/bcm_host.h")
    MESSAGE("bcm_host.h found")
    set(BCMHOST found)
    set(GLSystem "Embedded OpenGL" CACHE STRING "The OpenGL system to be used")
#-------------------------------------------------------------------------------
#check if we're running on OSMC Vero4K
elseif(EXISTS "${CMAKE_FIND_ROOT_PATH}/opt/vero3/lib/libMali.so")
    MESSAGE("libMali.so found")
    set(VERO4K found)
    set(GLSystem "Embedded OpenGL" CACHE STRING "The OpenGL system to be used")
#-------------------------------------------------------------------------------
#check if we're running on olinuxino / odroid / etc
elseif(EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/lib/libMali.so" OR
    EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/lib/arm-linux-gnueabihf/libMali.so" OR
    EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/lib/aarch64-linux-gnu/libMali.so" OR
    EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/local/lib/aarch64-linux-gnu/libmali.so" OR
    EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/lib/arm-linux-gnueabihf/mali-egl/libmali.so" OR
    EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/lib/arm-linux-gnueabihf/libmali.so")
    MESSAGE("libMali.so found")
    set(GLSystem "Embedded OpenGL" CACHE STRING "The OpenGL system to be used")
else()
    set(GLSystem "Desktop OpenGL" CACHE STRING "The OpenGL system to be used")
endif(GLES)

set_property(CACHE GLSystem PROPERTY STRINGS "Desktop OpenGL" "Embedded OpenGL")

#-------------------------------------------------------------------------------
# Use pkg-config for dependency resolution (preferred for cross-compilation)
find_package(PkgConfig REQUIRED)

# Core dependencies - pkg-config handles cross-compilation correctly
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(RAPIDJSON REQUIRED RapidJSON)

# FreeImage typically doesn't have pkg-config, use find_package as fallback
pkg_check_modules(FREEIMAGE freeimage)
if(NOT FREEIMAGE_FOUND)
    find_package(FreeImage REQUIRED)
    set(FREEIMAGE_INCLUDE_DIRS ${FreeImage_INCLUDE_DIRS})
    set(FREEIMAGE_LIBRARIES ${FreeImage_LIBRARIES})
endif()

# VLC is optional
pkg_check_modules(VLC libvlc)
if(VLC_FOUND)
    message(STATUS "VLC found - video playback enabled")
else()
    message(WARNING "VLC not found - video playback will be disabled")
    add_definitions(-DNO_VIDEO_PLAYER)
endif()

# OpenGL/EGL dependencies
if(${GLSystem} MATCHES "Desktop OpenGL")
    pkg_check_modules(OPENGL REQUIRED gl)
else()
    pkg_check_modules(EGL REQUIRED egl)
    pkg_check_modules(GLES2 REQUIRED glesv2)
endif()

# ALSA for Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    pkg_check_modules(ALSA REQUIRED alsa)
endif()

# libCEC support (optional)
if(CEC)
    pkg_check_modules(CEC IMPORTED_TARGET libcec)
    if(CEC_FOUND)
        add_definitions(-DHAVE_LIBCEC)
    endif()
endif()

#-------------------------------------------------------------------------------
#set up compiler flags and excutable names
if(DEFINED BCMHOST OR RPI)
    add_definitions(-D_RPI_)
endif()

if(DEFINED VERO4K)
    add_definitions(-D_VERO4K_)
endif()

#-------------------------------------------------------------------------------

if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
    add_definitions(-DNOMINMAX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP") #multi-processor compilation
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP") #multi-processor compilation
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
    #check for G++ 4.7+
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE G++_VERSION)
    if (G++_VERSION VERSION_LESS 4.7)
            message(SEND_ERROR "You need at least G++ 4.7 to compile EmulationStation!")
    endif()

    #set up compiler flags for GCC
    if (CMAKE_BUILD_TYPE MATCHES Debug)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wno-attributes -O0")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wno-attributes -O2")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O2")
    endif()
    
    # For cross-compilation, add library paths for proper dependency resolution
    if(CMAKE_CROSSCOMPILING)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_FIND_ROOT_PATH}/lib/aarch64-linux-gnu -L${CMAKE_FIND_ROOT_PATH}/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,${CMAKE_FIND_ROOT_PATH}/lib/aarch64-linux-gnu -Wl,-rpath-link,${CMAKE_FIND_ROOT_PATH}/usr/lib/aarch64-linux-gnu")
    endif()
endif()

if(${GLSystem} MATCHES "Desktop OpenGL")
    add_definitions(-DUSE_OPENGL_21)
else()
    add_definitions(-DUSE_OPENGLES_10)
endif()

#-------------------------------------------------------------------------------
#add include directories
set(COMMON_INCLUDE_DIRS
    ${FREETYPE_INCLUDE_DIRS}
    ${FREEIMAGE_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_MIXER_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${RAPIDJSON_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/es-core/src
)

if(VLC_FOUND)
    LIST(APPEND COMMON_INCLUDE_DIRS ${VLC_INCLUDE_DIRS})
endif()

if(CEC_FOUND)
    LIST(APPEND COMMON_INCLUDE_DIRS ${CEC_INCLUDE_DIRS})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    LIST(APPEND COMMON_INCLUDE_DIRS ${ALSA_INCLUDE_DIRS})
endif()

if(DEFINED BCMHOST)
    LIST(APPEND COMMON_INCLUDE_DIRS
        "${CMAKE_FIND_ROOT_PATH}/opt/vc/include"
        "${CMAKE_FIND_ROOT_PATH}/opt/vc/include/interface/vcos"
        "${CMAKE_FIND_ROOT_PATH}/opt/vc/include/interface/vmcs_host/linux"
        "${CMAKE_FIND_ROOT_PATH}/opt/vc/include/interface/vcos/pthreads"
    )
elseif(DEFINED VERO4K)
    LIST(APPEND COMMON_INCLUDE_DIRS
        "${CMAKE_FIND_ROOT_PATH}/opt/vero3/include"
    )
elseif(${GLSystem} MATCHES "Desktop OpenGL")
    LIST(APPEND COMMON_INCLUDE_DIRS ${OPENGL_INCLUDE_DIRS})
else()
    LIST(APPEND COMMON_INCLUDE_DIRS ${EGL_INCLUDE_DIRS} ${GLES2_INCLUDE_DIRS})
endif()

#-------------------------------------------------------------------------------
#add custom definitions
if(DEFINED SCREENSCRAPER_SOFTNAME)
    add_definitions(-DSCREENSCRAPER_SOFTNAME="${SCREENSCRAPER_SOFTNAME}")
endif()

if(DEFINED SCREENSCRAPER_DEV_LOGIN)
    add_definitions(-DSCREENSCRAPER_DEV_LOGIN="${SCREENSCRAPER_DEV_LOGIN}")
endif()

if(DEFINED GAMESDB_APIKEY)
    add_definitions(-DGAMESDB_APIKEY="${GAMESDB_APIKEY}")
endif()

#-------------------------------------------------------------------------------
#define libraries
set(COMMON_LIBRARIES
    ${FREETYPE_LIBRARIES}
    ${FREEIMAGE_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_MIXER_LIBRARIES}
    ${CURL_LIBRARIES}
    pugixml
    nanosvg
    pthread
)

if(VLC_FOUND)
    LIST(APPEND COMMON_LIBRARIES ${VLC_LIBRARIES})
endif()

if(CEC_FOUND)
    LIST(APPEND COMMON_LIBRARIES ${CEC_LIBRARIES})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    LIST(APPEND COMMON_LIBRARIES ${ALSA_LIBRARIES})
endif()

if(DEFINED BCMHOST)
    LIST(APPEND COMMON_LIBRARIES
        bcm_host
        brcmEGL
        ${GLES2_LIBRARIES}
        mali-bifrost-g31-rxp0-wayland-gbm
    )
elseif(DEFINED VERO4K)
    LIST(APPEND COMMON_LIBRARIES
        ${EGL_LIBRARIES}
        ${GLES2_LIBRARIES}
        mali-bifrost-g31-rxp0-wayland-gbm
    )
elseif(${GLSystem} MATCHES "Desktop OpenGL")
    LIST(APPEND COMMON_LIBRARIES ${OPENGL_LIBRARIES})
else()
    LIST(APPEND COMMON_LIBRARIES
        ${EGL_LIBRARIES}
        ${GLES2_LIBRARIES}
        mali-bifrost-g31-rxp0-wayland-gbm
    )
endif()

#-------------------------------------------------------------------------------
# Link directories (only for special cases like BCMHOST/VERO4K)
if(DEFINED BCMHOST)
    link_directories("${CMAKE_FIND_ROOT_PATH}/opt/vc/lib")
elseif(DEFINED VERO4K)
    link_directories("${CMAKE_FIND_ROOT_PATH}/opt/vero3/lib")
endif()

#-------------------------------------------------------------------------------
# set up build directories
set(dir ${CMAKE_CURRENT_SOURCE_DIR})
set(EXECUTABLE_OUTPUT_PATH ${dir} CACHE PATH "Build directory" FORCE)
set(LIBRARY_OUTPUT_PATH ${dir} CACHE PATH "Build directory" FORCE)

#-------------------------------------------------------------------------------
# add each component
add_subdirectory("external")
add_subdirectory("es-core")
add_subdirectory("es-app")
