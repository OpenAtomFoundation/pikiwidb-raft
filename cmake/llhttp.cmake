# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

# nodejs/llhttp
SET(LLHTTP_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "llhttp include directory." FORCE)
SET(LLHTTP_LIBRARIES "${LIB_INSTALL_DIR}/libllhttp.a" CACHE FILEPATH "llhttp library." FORCE)
ExternalProject_Add(
        extern_llhttp
        ${EXTERNAL_PROJECT_LOG_ARGS}
        URL https://github.com/nodejs/llhttp/archive/refs/tags/release/v6.0.5.tar.gz
        URL_HASH MD5=7ec6829c56642cce27e3d8e06504ddca
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
)

ADD_DEPENDENCIES(extern_llhttp snappy gflags zlib)
ADD_LIBRARY(llhttp STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET llhttp PROPERTY IMPORTED_LOCATION ${LLHTTP_LIBRARIES})
ADD_DEPENDENCIES(llhttp extern_llhttp)
