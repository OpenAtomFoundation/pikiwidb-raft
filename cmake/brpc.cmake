# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(BRPC_SOURCES_DIR ${LIB_INSTALL_PREFIX})
SET(BRPC_INSTALL_DIR ${LIB_INSTALL_PREFIX})
SET(BRPC_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "brpc include directory." FORCE)
SET(BRPC_LIBRARIES "${LIB_INSTALL_DIR}/libbrpc.a" CACHE FILEPATH "brpc library." FORCE)

SET(CMAKE_FIND_LIBRARY_SUFFIXES ${LIB_INSTALL_PREFIX})

# Reference https://stackoverflow.com/questions/45414507/pass-a-list-of-prefix-paths-to-externalproject-add-in-cmake-args
SET(CMAKE_CPP_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
# If minimal .a is need, you can set  WITH_DEBUG_SYMBOLS=OFF
ExternalProject_Add(
        extern_brpc
        ${EXTERNAL_PROJECT_LOG_ARGS}
        DEPENDS ssl crypto zlib protobuf leveldb gflags
        URL https://github.com/apache/brpc/archive/refs/tags/1.8.0.tar.gz
        URL_HASH SHA256=13ffb2f1f57c679379a20367c744b3e597614a793ec036cd7580aae90798019d
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DCMAKE_CPP_FLAGS=${CMAKE_CPP_FLAGS}
        -DCMAKE_INSTALL_PREFIX=${BRPC_INSTALL_DIR}
        -DCMAKE_FIND_LIBRARY_SUFFIXES=${LIB_INSTALL_PREFIX}
        -DGFLAGS_INCLUDE_PATH=${GFLAGS_INCLUDE_DIR}
        -DBUILD_SHARED_LIBS=FALSE
        -DBUILD_BRPC_TOOLS=OFF
        -DGFLAGS_LIBRARY=${GFLAGS_LIBRARIES}

        -DLEVELDB_LIB=${LEVELDB_LIBRARIES}
        -DLEVELDB_INCLUDE_PATH=${LEVELDB_INCLUDE_DIR}

        -DPROTOC_LIB=${PROTOC_LIBRARY}
        -DPROTOBUF_LIBRARIES=${PROTOBUF_LIBRARY}
        -DPROTOBUF_INCLUDE_DIRS=${PROTOBUF_INCLUDE_DIR}
        -DPROTOBUF_PROTOC_EXECUTABLE=${PROTOBUF_PROTOC}

        -DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DPROJECT_BINARY_DIR=${LIB_INSTALL_PREFIX}
        -DWITH_GLOG=OFF
        -DDOWNLOAD_GTEST=OFF
        BUILD_COMMAND make -j${CPU_CORE}
)
ADD_DEPENDENCIES(extern_brpc ssl crypto zlib protobuf leveldb gflags)
ADD_LIBRARY(brpc STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET brpc PROPERTY IMPORTED_LOCATION ${BRPC_LIBRARIES})
ADD_DEPENDENCIES(brpc extern_brpc)
