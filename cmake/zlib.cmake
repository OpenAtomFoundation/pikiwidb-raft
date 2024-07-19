# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(ZLIB_SOURCES_DIR ${LIB_INSTALL_PREFIX})
SET(ZLIB_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "zlib include directory." FORCE)
SET(ZLIB_LIBRARIES "${LIB_INSTALL_DIR}/libz.a" CACHE FILEPATH "zlib library." FORCE)

ExternalProject_Add(
        extern_zlib
        ${EXTERNAL_PROJECT_LOG_ARGS}
        GIT_REPOSITORY  "https://github.com/madler/zlib.git"
        GIT_TAG         "v1.2.8"
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

ADD_LIBRARY(zlib STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET zlib PROPERTY IMPORTED_LOCATION ${ZLIB_LIBRARIES})
ADD_DEPENDENCIES(zlib extern_zlib)