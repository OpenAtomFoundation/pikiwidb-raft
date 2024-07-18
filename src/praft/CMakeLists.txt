# Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

ADD_CUSTOM_COMMAND(
  OUTPUT "${PROTO_OUTPUT_DIR}/binlog.pb.cc"
  DEPENDS protobuf
  COMMAND ${PROTOBUF_PROTOC}
  ARGS -I ${CMAKE_CURRENT_SOURCE_DIR}
    --cpp_out ${PROTO_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/binlog.proto
)
ADD_LIBRARY(binlog_pb STATIC "${PROTO_OUTPUT_DIR}/binlog.pb.cc")
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
TARGET_INCLUDE_DIRECTORIES(binlog_pb PRIVATE ${PROTOBUF_INCLUDE_DIR})

ADD_CUSTOM_COMMAND(
  OUTPUT "${PROTO_OUTPUT_DIR}/praft.pb.cc"
  DEPENDS protobuf
  COMMAND ${PROTOBUF_PROTOC}
  ARGS -I ${CMAKE_CURRENT_SOURCE_DIR}
    --cpp_out ${PROTO_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/praft.proto
)

ADD_LIBRARY(praft_pb STATIC "${PROTO_OUTPUT_DIR}/praft.pb.cc")
TARGET_INCLUDE_DIRECTORIES(praft_pb PRIVATE ${PROTOBUF_INCLUDE_DIR})

FILE(GLOB PRAFT_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})

ADD_LIBRARY(praft ${PRAFT_SRC})

TARGET_INCLUDE_DIRECTORIES(praft
        PRIVATE ${PROJECT_SOURCE_DIR}/src
        PRIVATE ${rocksdb_SOURCE_DIR}/include
        PRIVATE ${BRAFT_INCLUDE_DIR}
        PRIVATE ${BRPC_INCLUDE_DIR}
        PRIVATE ${PROTO_OUTPUT_DIR}
)

IF (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    SET(PRAFT_LIB ${PRAFT_LIB} rt)
ENDIF ()

SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
ADD_DEPENDENCIES(praft protobuf praft_pb binlog_pb)
TARGET_LINK_LIBRARIES(praft net; dl; fmt; storage; pstd braft brpc ssl crypto zlib protobuf leveldb gflags rocksdb z ${PRAFT_LIB})
SET_TARGET_PROPERTIES(praft PROPERTIES LINKER_LANGUAGE CXX)
