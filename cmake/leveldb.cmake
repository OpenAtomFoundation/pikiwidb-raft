# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(LEVELDB_INCLUDE_DIR "${LIB_INCLUDE_DIR}/leveldb" CACHE PATH "leveldb include directory." FORCE)
SET(LEVELDB_LIBRARIES "${LIB_INSTALL_DIR}/libleveldb.a" CACHE FILEPATH "leveldb include directory." FORCE)
SET(LEVELDB_INSTALL_LIBDIR "${LIB_INSTALL_PREFIX}/lib")

ExternalProject_Add(
        extern_leveldb
        ${EXTERNAL_PROJECT_LOG_ARGS}
        DEPENDS snappy
        GIT_REPOSITORY "https://github.com/google/leveldb.git"
        GIT_TAG "1.23"
        CMAKE_ARGS
        -DCMAKE_INSTALL_LIBDIR=${LEVELDB_INSTALL_LIBDIR}
        -DCMAKE_INSTALL_INCLUDEDIR=${LEVELDB_INCLUDE_DIR}
        -DCMAKE_FIND_LIBRARY_SUFFIXES=${LIB_INSTALL_PREFIX}
        -DSnappy_INCLUDE_DIR=${Snappy_INCLUDE_DIRS}
        -DSnappy_LIBRARIES=${Snappy_LIBRARIES}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DLEVELDB_BUILD_TESTS=OFF
        -DLEVELDB_BUILD_BENCHMARKS=OFF
        -DCMAKE_BUILD_TYPE=${THIRD_PARTY_BUILD_TYPE}
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_DEPENDENCIES(extern_leveldb snappy)
ADD_LIBRARY(leveldb STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET leveldb PROPERTY IMPORTED_LOCATION ${LEVELDB_LIBRARIES})
ADD_DEPENDENCIES(leveldb extern_leveldb)
