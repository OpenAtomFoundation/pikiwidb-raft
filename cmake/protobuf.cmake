# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

# Always invoke `FIND_PACKAGE(Protobuf)` for importing function protobuf_generate_cpp

IF (${LIB_BUILD_TYPE} STREQUAL DEBUG)
    SET(LIB_PROTOBUF "libprotobufd.a")
    SET(LIB_PROTOC "libprotocd.a")
ELSE ()
    SET(LIB_PROTOBUF "libprotobuf.a")
    SET(LIB_PROTOC "libprotoc.a")
ENDIF ()

SET(PROTOBUF_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "protobuf include directory." FORCE)
SET(PROTOBUF_LIBRARY "${LIB_INSTALL_DIR}/${LIB_PROTOBUF}" CACHE FILEPATH "protobuf install directory." FORCE)
SET(PROTOC_LIBRARY "${LIB_INSTALL_DIR}/${LIB_PROTOC}" CACHE FILEPATH "protoc install directory." FORCE)
SET(PROTOBUF_PROTOC "${LIB_INSTALL_PREFIX}/bin/protoc")

ExternalProject_Add(
        extern_protobuf
        DOWNLOAD_NO_PROGRESS 1
        UPDATE_COMMAND ""
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        SOURCE_SUBDIR cmake
        DEPENDS zlib
        URL "https://github.com/protocolbuffers/protobuf/archive/v3.18.0.tar.gz"
        URL_HASH SHA256=14e8042b5da37652c92ef6a2759e7d2979d295f60afd7767825e3de68c856c54
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=${LIB_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -Dprotobuf_BUILD_TESTS=OFF
        -Dprotobuf_BUILD_LIBPROTOC=ON
        BUILD_COMMAND make -j${CPU_CORE}
)

ADD_LIBRARY(protobuf STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET protobuf PROPERTY IMPORTED_LOCATION ${PROTOBUF_LIBRARY})
ADD_DEPENDENCIES(protobuf extern_protobuf)
