# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

FILE(GLOB STORAGE_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/storage/*.h"
)
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
ADD_LIBRARY(storage ${STORAGE_SRC})

TARGET_INCLUDE_DIRECTORIES(storage
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${ROCKSDB_SOURCES_DIR}
        PRIVATE ${ROCKSDB_SOURCES_DIR}/include
        PRIVATE ${PROTO_OUTPUT_DIR}
        PRIVATE ${PROTOBUF_INCLUDE_DIR}
        PRIVATE ${SPDLOG_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(storage
        pstd
        braft
        brpc
        ssl
        crypto
        zlib
        leveldb
        gflags
        rocksdb
        protobuf
        spdlog
)

SET_TARGET_PROPERTIES(storage PROPERTIES LINKER_LANGUAGE CXX)

ADD_SUBDIRECTORY(tests)

ADD_DEPENDENCIES(storage
        pstd
        braft
        brpc
        ssl
        crypto
        zlib
        leveldb
        gflags
        rocksdb
        protobuf
        spdlog
)
