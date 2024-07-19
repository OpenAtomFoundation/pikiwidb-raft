# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(ROCKSDB_SOURCES_DIR "${LIB_SOURCE_DIR}/extern_rocksdb" CACHE PATH "Path to RocksDB sources")
SET(ROCKSDB_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "rocksdb include directory." FORCE)
SET(ROCKSDB_LIBRARIES "${LIB_INSTALL_DIR}/librocksdb.a" CACHE FILEPATH "rocksdb include directory." FORCE)

ExternalProject_Add(
        extern_rocksdb
        ${EXTERNAL_PROJECT_LOG_ARGS}
        GIT_REPOSITORY https://github.com/facebook/rocksdb.git
        GIT_TAG v8.3.3
        DEPENDS
        gflags
        snappy
        zlib
        lz4
        zstd
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DWITH_BENCHMARK=OFF
        -DWITH_BENCHMARK_TOOLS=OFF
        -DWITH_TOOLS=OFF
        -DWITH_CORE_TOOLS=OFF
        -DWITH_TESTS=OFF
        -DWITH_TRACE_TOOLS=OFF
        -DWITH_EXAMPLES=OFF
        -DROCKSDB_BUILD_SHARED=OFF
        -DWITH_JEMALLOC=${JEMALLOC_ON}
        -DFAIL_ON_WARNINGS=OFF
        -DWITH_LIBURING=OFF
        -DWITH_TESTS=OFF
        -DWITH_LZ4=ON
        -DWITH_SNAPPY=ON
        -DWITH_ZLIB=ON
        -DWITH_ZSTD=ON
        -DWITH_GFLAGS=ON
        -DUSE_RTTI=ON
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_DEPENDENCIES(extern_rocksdb snappy gflags zlib lz4)
ADD_LIBRARY(rocksdb STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET rocksdb PROPERTY IMPORTED_LOCATION ${ROCKSDB_LIBRARIES})
ADD_DEPENDENCIES(rocksdb extern_rocksdb)
