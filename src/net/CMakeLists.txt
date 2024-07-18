# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

AUX_SOURCE_DIRECTORY(. NET_SRC)
AUX_SOURCE_DIRECTORY(./lzf NET_SRC)
LIST(FILTER NET_SRC EXCLUDE REGEX ".*_test.cc")
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
ADD_LIBRARY(net ${NET_SRC})

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src/pstd)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src/net)

message(STATUS "LIBEVENT_INCLUDE_DIRS: ${LIBEVENT_INCLUDE_DIRS}")
TARGET_INCLUDE_DIRECTORIES(net
        PRIVATE ${LIB_INCLUDE_DIR}
        PUBLIC ${LIBEVENT_INCLUDE_DIRS}
)

TARGET_LINK_LIBRARIES(net; pstd event_extra event_core llhttp)

SET_TARGET_PROPERTIES(net PROPERTIES LINKER_LANGUAGE CXX)

ADD_DEPENDENCIES(net pstd)
