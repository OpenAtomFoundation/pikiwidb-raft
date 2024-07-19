# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.


IF (${LIB_BUILD_TYPE} STREQUAL DEBUG)
    SET(LIB_FMT libfmtd.a)
ELSE ()
    SET(LIB_FMT libfmt.a)
ENDIF ()

SET(FMT_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "fmt include directory." FORCE)
SET(FMT_LIBRARIES "${LIB_INSTALL_DIR}/${LIB_FMT}" CACHE FILEPATH "fmt library directory." FORCE)

ExternalProject_Add(
        extern_fmt
        URL https://github.com/fmtlib/fmt/archive/10.1.1.zip
        URL_HASH SHA256=3c2e73019178ad72b0614a3124f25de454b9ca3a1afe81d5447b8d3cbdb6d322
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DFMT_DOC=FALSE
        -DFMT_TEST=FALSE
        -DSHARED_LIBS=FALSE
        BUILD_COMMAND make -j${CPU_CORE}
)


ADD_LIBRARY(fmt STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET fmt PROPERTY IMPORTED_LOCATION ${FMT_LIBRARIES})
ADD_DEPENDENCIES(fmt extern_fmt)
