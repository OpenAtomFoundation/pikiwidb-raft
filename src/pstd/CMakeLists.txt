# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

AUX_SOURCE_DIRECTORY(. STD_SRC)
SET(LIBRARY_OUTPUT_PATH ${PLIB_INSTALL_DIR})
SET(PSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "pstd include directory." FORCE)
ADD_LIBRARY(pstd ${STD_SRC})

ADD_SUBDIRECTORY(tests)

TARGET_INCLUDE_DIRECTORIES(pstd
        PUBLIC ${ROCKSDB_SOURCES_DIR}/include
        PUBLIC ${LIB_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(pstd; spdlog pthread fmt)
SET_TARGET_PROPERTIES(pstd PROPERTIES LINKER_LANGUAGE CXX)
ADD_DEPENDENCIES(pstd rocksdb spdlog)
