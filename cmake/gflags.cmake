# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

IF (${LIB_BUILD_TYPE} STREQUAL DEBUG)
    SET(LIB_GFLAGS libgflags_debug.a)
ELSE ()
    SET(LIB_GFLAGS libgflags.a)
ENDIF ()

SET(GFLAGS_ROOT ${LIB_INSTALL_PREFIX} CACHE PATH "gflags source directory." FORCE)
SET(GFLAGS_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "gflags include directory." FORCE)
SET(GFLAGS_LIBRARIES "${LIB_INSTALL_DIR}/${LIB_GFLAGS}" CACHE FILEPATH "gflags library." FORCE)
SET(GFLAGS_LIBRARY "${LIB_INSTALL_DIR}/${LIB_GFLAGS}" CACHE FILEPATH "gflags library." FORCE)

SET(GFLAGS_INCLUDE_PATH "${LIB_INCLUDE_DIR}/gflags" CACHE PATH "gflags include directory." FORCE)

ExternalProject_Add(
        extern_gflags
        URL https://github.com/gflags/gflags/archive/v2.2.2.zip
        URL_HASH SHA256=19713a36c9f32b33df59d1c79b4958434cb005b5b47dc5400a7a4b078111d9b5
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -DGFLAGS_BUILD_STATIC_LIBS=ON
        -DGFLAGS_BUILD_SHARED_LIBS=OFF
        -DGFLAGS_BUILD_gflags_LIB=ON
        -DGFLAGS_BUILD_gflags_nothreads_LIB=ON
        -DGFLAGS_NAMESPACE=gflags
        -DGFLAGS_BUILD_TESTING=OFF
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_LIBRARY(gflags STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET gflags PROPERTY IMPORTED_LOCATION ${GFLAGS_LIBRARIES})
ADD_DEPENDENCIES(gflags extern_gflags)
