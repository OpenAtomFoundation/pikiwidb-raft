# Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

FILE(GLOB PCACHE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})

ADD_LIBRARY(pcache ${PCACHE_SRC})

TARGET_INCLUDE_DIRECTORIES(pcache
        PRIVATE ${PROJECT_SOURCE_DIR}/src
        PRIVATE ${rocksdb_SOURCE_DIR}/include
        PRIVATE ${REDISCACHE_INCLUDE_DIR}
        PRIVATE ${PROJECT_SOURCE_DIR}/src/pstd
        PRIVATE ${PROJECT_SOURCE_DIR}/src/storage/include
)

ADD_DEPENDENCIES(pcache rediscache storage )
             
TARGET_LINK_LIBRARIES(pcache pstd rediscache gflags rocksdb storage)
        
SET_TARGET_PROPERTIES(pcache PROPERTIES LINKER_LANGUAGE CXX)
