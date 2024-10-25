# Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(REDISCACHE_SOURCES_DIR "${LIB_INSTALL_PREFIX}" CACHE PATH "rediscache source directory." FORCE)
SET(REDISCACHE_INSTALL_DIR "${LIB_INSTALL_PREFIX}" CACHE PATH "rediscache install directory." FORCE)
SET(REDISCACHE_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "rediscache include directory." FORCE)
SET(REDISCACHE_LIBRARIES "${LIB_INSTALL_DIR}/librediscache.a" CACHE FILEPATH "rediscache library." FORCE)

ExternalProject_Add(
  extern_rediscache
  ${EXTERNAL_PROJECT_LOG_ARGS}
  #URL https://github.com/pikiwidb/rediscache/archive/refs/tags/v1.0.7.tar.gz
  #URL_HASH MD5=02c8aadc018dd8d4d3803cc420d1d75b
  #temp used
  GIT_REPOSITORY https://github.com/hahahashen/rediscache.git
  GIT_TAG feat/removeUseTcMallocMacroDefinition
  CMAKE_ARGS 
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DPROJECT_BINARY_DIR=${LIB_INSTALL_PREFIX}
  -DCMAKE_FIND_LIBRARY_SUFFIXES=${LIB_INSTALL_PREFIX}
  -DSNAPPY_BUILD_TESTS=OFF
  -DCMAKE_INSTALL_INCLUDEDIR=${REDISCACHE_INCLUDE_DIR}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  BUILD_COMMAND make -j${CPU_CORE}
)

ADD_LIBRARY(rediscache STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET rediscache PROPERTY IMPORTED_LOCATION ${REDISCACHE_LIBRARIES})
ADD_DEPENDENCIES(rediscache extern_rediscache)

