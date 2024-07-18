# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

AUX_SOURCE_DIRECTORY(. STD_SRC)
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
ADD_LIBRARY(pstd ${STD_SRC})

ADD_SUBDIRECTORY(tests)

TARGET_INCLUDE_DIRECTORIES(pstd
        PRIVATE ${ROCKSDB_SOURCES_DIR}/include
        PRIVATE ${LIB_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(pstd; spdlog pthread)
SET_TARGET_PROPERTIES(pstd PROPERTIES LINKER_LANGUAGE CXX)
ADD_DEPENDENCIES(pstd rocksdb spdlog)
