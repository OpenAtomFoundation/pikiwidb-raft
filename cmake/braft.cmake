# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

INCLUDE(ExternalProject)

#SET(BRAFT_SOURCES_DIR ${THIRD_PARTY_PATH}/braft)
#SET(BRAFT_INSTALL_DIR ${THIRD_PARTY_PATH}/install/braft)
#SET(BRAFT_INCLUDE_DIR "${BRAFT_INSTALL_DIR}/include" CACHE PATH "braft include directory." FORCE)
#SET(BRAFT_LIBRARIES "${BRAFT_INSTALL_DIR}/lib/libbraft.a" CACHE FILEPATH "braft library." FORCE)

SET(BRAFT_SOURCES_DIR ${LIB_INSTALL_PREFIX})
SET(BRAFT_INSTALL_DIR ${LIB_INSTALL_PREFIX})
SET(BRAFT_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "brpc include directory." FORCE)
SET(BRAFT_LIBRARIES "${LIB_INSTALL_DIR}/libbraft.a" CACHE FILEPATH "brpc library." FORCE)

#SET(prefix_path "${THIRD_PARTY_PATH}/install/brpc|${CMAKE_CURRENT_BINARY_DIR}/_deps/gflags-build|${THIRD_PARTY_PATH}/install/protobuf|${THIRD_PARTY_PATH}/install/zlib|${THIRD_PARTY_PATH}/install/leveldb")

IF(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    SET(BRAFT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-U,__Z13GetStackTracePPvii")
ENDIF()

ExternalProject_Add(
        extern_braft
        ${EXTERNAL_PROJECT_LOG_ARGS}
        DEPENDS brpc
        GIT_REPOSITORY "https://github.com/pikiwidb/braft.git"
        GIT_TAG master
        GIT_SHALLOW true
#        PREFIX ${BRAFT_SOURCES_DIR}
#        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
#        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
#        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_FLAGS=${BRAFT_CXX_FLAGS}
#        -DCMAKE_C_FLAGS=${BRAFT_C_FLAGS}
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
#        -DCMAKE_INSTALL_LIBDIR=${BRAFT_INSTALL_DIR}/lib
        -DBRPC_LIB=${BRPC_LIBRARIES}
        -DBRPC_INCLUDE_PATH=${BRPC_INCLUDE_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
#        -DCMAKE_PREFIX_PATH=${prefix_path}
        -DBRPC_WITH_GLOG=OFF
#        -DOPENSSL_ROOT_DIR=${THIRD_PARTY_PATH}/install/openssl
#        BUILD_IN_SOURCE 1
        -DCMAKE_FIND_LIBRARY_SUFFIXES=${LIB_INSTALL_PREFIX}
        -DLEVELDB_LIB=${LEVELDB_LIBRARIES}
        -DLEVELDB_INCLUDE_PATH=${LEVELDB_INCLUDE_DIR}

        -DGFLAGS_INCLUDE_PATH=${GFLAGS_INCLUDE_DIR}
        -DGFLAGS_LIB=${GFLAGS_LIBRARIES}

        -DPROTOC_LIB=${PROTOC_LIBRARY}
        -DPROTOBUF_LIBRARIES=${PROTOBUF_LIBRARY}
        -DPROTOBUF_INCLUDE_DIRS=${PROTOBUF_INCLUDE_DIR}
        -DPROTOBUF_PROTOC_EXECUTABLE=${PROTOBUF_PROTOC}
        -DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_COMMAND make -j${CPU_CORE}
#        INSTALL_COMMAND mkdir -p ${BRAFT_INSTALL_DIR}/lib/ COMMAND cp ${BRAFT_SOURCES_DIR}/src/extern_braft/output/lib/libbraft.a ${BRAFT_LIBRARIES} COMMAND rm -rf ${BRAFT_INCLUDE_DIR} COMMAND cp -r ${BRAFT_SOURCES_DIR}/src/extern_braft/output/include ${BRAFT_INCLUDE_DIR}
)

ADD_DEPENDENCIES(extern_braft brpc gflags)
ADD_LIBRARY(braft STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET braft PROPERTY IMPORTED_LOCATION ${BRAFT_LIBRARIES})
ADD_DEPENDENCIES(braft extern_braft)
