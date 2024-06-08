# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

INCLUDE_GUARD()

SET(GFLAGS_BUILD_TYPE "Release")
SET(CMAKE_BUILD_TYPE ${GFLAGS_BUILD_TYPE})

FetchContent_Declare(gflags
  URL https://github.com/gflags/gflags/archive/v2.2.2.zip
  URL_HASH SHA256=19713a36c9f32b33df59d1c79b4958434cb005b5b47dc5400a7a4b078111d9b5
)

SET(GFLAGS_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
SET(GFLAGS_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
SET(GFLAGS_BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
SET(GFLAGS_BUILD_gflags_nothreads_LIB OFF CACHE BOOL "" FORCE)
SET(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FETCHCONTENT_MAKEAVAILABLE(gflags)

FIND_PACKAGE(Threads REQUIRED)

TARGET_LINK_LIBRARIES(gflags_static Threads::Threads)

SET(GFLAGS_INCLUDE_PATH ${CMAKE_CURRENT_BINARY_DIR}/_deps/gflags-build/include CACHE PATH "" FORCE)
SET(GFLAGS_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/_deps/gflags-build/libgflags.a CACHE PATH "" FORCE)
SET(GFLAGS_LIB ${CMAKE_CURRENT_BINARY_DIR}/_deps/gflags-build/libgflags.a CACHE PATH "" FORCE)

SET(CMAKE_BUILD_TYPE ${THIRD_PARTY_BUILD_TYPE})