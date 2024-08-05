# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

INCLUDE(ExternalProject)

SET(BRAFT_SOURCES_DIR ${LIB_INSTALL_PREFIX})
SET(BRAFT_INSTALL_DIR ${LIB_INSTALL_PREFIX})
SET(BRAFT_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "brpc include directory." FORCE)
SET(BRAFT_LIBRARIES "${LIB_INSTALL_DIR}/libbraft.a" CACHE FILEPATH "brpc library." FORCE)

IF (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    SET(BRAFT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-U,__Z13GetStackTracePPvii")
ENDIF ()

ExternalProject_Add(
        extern_braft
        ${EXTERNAL_PROJECT_LOG_ARGS}
        DEPENDS brpc
        # The pr on braft is not merged, so I am using my own warehouse to run the test for the time being
        GIT_REPOSITORY "https://github.com/pikiwidb/braft.git"
        GIT_TAG v1.1.2-alpha2
        GIT_SHALLOW true
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DCMAKE_CXX_FLAGS=${BRAFT_CXX_FLAGS}
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_LIBRARY_PATH=${LIB_INSTALL_PREFIX}
        -DBRPC_LIB=${BRPC_LIBRARIES}
        -DBRPC_INCLUDE_PATH=${BRPC_INCLUDE_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBRPC_WITH_GLOG=OFF
        -DCMAKE_FIND_LIBRARY_SUFFIXES=${LIB_INSTALL_PREFIX}
        -DLEVELDB_WITH_SNAPPY=ON
        -DLEVELDB_LIB=${LEVELDB_LIBRARIES}
        -DLEVELDB_INCLUDE_PATH=${LEVELDB_INCLUDE_DIR}

        -DGFLAGS_INCLUDE_PATH=${GFLAGS_INCLUDE_DIR}
        -DGFLAGS_LIB=${GFLAGS_LIBRARIES}

        -DPROTOC_LIB=${PROTOC_LIBRARY}
        -DPROTOBUF_LIBRARY=${PROTOBUF_LIBRARY}
        -DPROTOBUF_INCLUDE_DIRS=${PROTOBUF_INCLUDE_DIR}
        -DPROTOBUF_PROTOC_EXECUTABLE=${PROTOBUF_PROTOC}
        -DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_DEPENDENCIES(extern_braft brpc gflags)
ADD_LIBRARY(braft STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET braft PROPERTY IMPORTED_LOCATION ${BRAFT_LIBRARIES})
ADD_DEPENDENCIES(braft extern_braft)
