# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

IF (BUILD_TYPE STREQUAL DEBUG)
    SET(SPDLOG_LIB "libspdlogd.a")
ELSE ()
    SET(SPDLOG_LIB "libspdlog.a")
ENDIF ()

SET(SPDLOG_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "spdlog include directory." FORCE)
SET(SPDLOG_LIBRARIES "${LIB_INSTALL_DIR}/${SPDLOG_LIB}" CACHE FILEPATH "spdlog library directory." FORCE)

ADD_DEFINITIONS(-DSPDLOG_FMT_EXTERNAL)

ExternalProject_Add(
        extern_spdlog
        ${EXTERNAL_PROJECT_LOG_ARGS}
        URL https://github.com/gabime/spdlog/archive/v1.12.0.zip
        URL_HASH SHA256=6174BF8885287422A6C6A0312EB8A30E8D22BCFCEE7C48A6D02D1835D7769232
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_FMT_EXTERNAL=ON
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_DEPENDENCIES(extern_spdlog fmt)
ADD_LIBRARY(spdlog STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET spdlog PROPERTY IMPORTED_LOCATION ${SPDLOG_LIBRARIES})
ADD_DEPENDENCIES(spdlog extern_spdlog)
