#snapped from: https://bitbucket.org/sinbad/ogre/src/0bba4f7cdb95/CMake/Packages/FindFreeImage.cmake?at=default
#-------------------------------------------------------------------
# This file is part of the CMake build system for OGRE
#     (Object-oriented Graphics Rendering Engine)
# For the latest info, see httpwww.ogre3d.org
#
# The contents of this file are placed in the public domain. Feel
# free to make use of it in any way you like.
#-------------------------------------------------------------------

# - Try to find FreeImage
# Once done, this will define
#
#  FreeImage_FOUND - system has FreeImage
#  FreeImage_INCLUDE_DIRS - the FreeImage include directories 
#  FreeImage_LIBRARIES - link these to use FreeImage
#
# Modified for cross-compilation support - 2026

include(FindPkgMacros)
findpkg_begin(FreeImage)

# Get path, convert backslashes as ${ENV_${var}}
getenv_path(FREEIMAGE_HOME)

# Try pkg-config first (works well for cross-compilation)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_FreeImage QUIET freeimage)
endif()

# construct search paths
set(FreeImage_PREFIX_PATH ${FREEIMAGE_HOME} ${ENV_FREEIMAGE_HOME})

# Add CMAKE_FIND_ROOT_PATH paths for cross-compilation
if(CMAKE_FIND_ROOT_PATH)
  foreach(root ${CMAKE_FIND_ROOT_PATH})
    list(APPEND FreeImage_PREFIX_PATH
      ${root}/usr
      ${root}/usr/local
    )
  endforeach()
endif()

create_search_paths(FreeImage)

# Add pkg-config paths as hints
if(PC_FreeImage_INCLUDE_DIRS)
  list(APPEND FreeImage_INC_SEARCH_PATH ${PC_FreeImage_INCLUDE_DIRS})
endif()
if(PC_FreeImage_LIBRARY_DIRS)
  list(APPEND FreeImage_LIB_SEARCH_PATH ${PC_FreeImage_LIBRARY_DIRS})
endif()

# redo search if prefix path changed
clear_if_changed(FreeImage_PREFIX_PATH
  FreeImage_LIBRARY_FWK
  FreeImage_LIBRARY_REL
  FreeImage_LIBRARY_DBG
  FreeImage_INCLUDE_DIR
)

set(FreeImage_LIBRARY_NAMES freeimage freeimageLib)
get_debug_names(FreeImage_LIBRARY_NAMES)

use_pkgconfig(FreeImage_PKGC freeimage)

findpkg_framework(FreeImage)

find_path(FreeImage_INCLUDE_DIR NAMES FreeImage.h HINTS ${FreeImage_INC_SEARCH_PATH} ${FreeImage_PKGC_INCLUDE_DIRS})

find_library(FreeImage_LIBRARY_REL NAMES ${FreeImage_LIBRARY_NAMES} HINTS ${FreeImage_LIB_SEARCH_PATH} ${FreeImage_PKGC_LIBRARY_DIRS} PATH_SUFFIXES release relwithdebinfo minsizerel)
find_library(FreeImage_LIBRARY_DBG NAMES ${FreeImage_LIBRARY_NAMES_DBG} HINTS ${FreeImage_LIB_SEARCH_PATH} ${FreeImage_PKGC_LIBRARY_DIRS} PATH_SUFFIXES debug)

make_library_set(FreeImage_LIBRARY)

findpkg_finish(FreeImage)
