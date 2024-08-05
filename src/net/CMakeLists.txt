# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

CMAKE_MINIMUM_REQUIRED(VERSION 3.25)

PROJECT(net)

message(STATUS "PSTD_INCLUDE_DIR: ${PSTD_INCLUDE_DIR}")
SET(CMAKE_CXX_STANDARD 20)

AUX_SOURCE_DIRECTORY(. NET_SRC)
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})

ADD_LIBRARY(net ${NET_SRC})

TARGET_INCLUDE_DIRECTORIES(net
        PRIVATE ${PSTD_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(net pstd)
