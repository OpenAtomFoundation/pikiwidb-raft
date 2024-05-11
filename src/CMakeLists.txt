# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

AUX_SOURCE_DIRECTORY(. PIKIWIDB_SRC)

ADD_EXECUTABLE(pikiwidb ${PIKIWIDB_SRC})

IF(CMAKE_BUILD_TYPE STREQUAL "Release")
    # get current date and time and git commit id
    EXECUTE_PROCESS(COMMAND date "+%Y-%m-%d_%H:%M:%S" OUTPUT_VARIABLE BUILD_TIMESTAMP OUTPUT_STRIP_TRAILING_WHITESPACE)
    EXECUTE_PROCESS(COMMAND git rev-parse HEAD OUTPUT_VARIABLE GIT_COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)

    MESSAGE(STATUS "Build timestamp: ${BUILD_TIMESTAMP}")
    MESSAGE(STATUS "Git commit id: ${GIT_COMMIT_ID}")

    TARGET_COMPILE_DEFINITIONS(pikiwidb PRIVATE KPIKIWIDB_BUILD_DATE="${BUILD_TIMESTAMP}" KPIKIWIDB_GIT_COMMIT_ID="${GIT_COMMIT_ID}")
ENDIF()

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

TARGET_INCLUDE_DIRECTORIES(pikiwidb
        PRIVATE ${PROJECT_SOURCE_DIR}/src
        PRIVATE ${PROJECT_SOURCE_DIR}/src/pstd
        PRIVATE ${PROJECT_SOURCE_DIR}/src/net
        PRIVATE ${PROJECT_SOURCE_DIR}/src/storage/include
        PRIVATE ${rocksdb_SOURCE_DIR}/
        PRIVATE ${rocksdb_SOURCE_DIR}/include
        PRIVATE ${BRAFT_INCLUDE_DIR}
        PRIVATE ${BRPC_INCLUDE_DIR}
        )


TARGET_LINK_LIBRARIES(pikiwidb net; dl; fmt; storage; rocksdb; pstd braft brpc ssl crypto zlib protobuf leveldb gflags  z praft praft_pb "${LIB}")

SET_TARGET_PROPERTIES(pikiwidb PROPERTIES LINKER_LANGUAGE CXX)