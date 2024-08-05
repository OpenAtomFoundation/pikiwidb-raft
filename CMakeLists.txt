# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

CMAKE_MINIMUM_REQUIRED(VERSION 3.14)
PROJECT(PikiwiDB)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type")

# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
IF (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    CMAKE_POLICY(SET CMP0135 NEW)
ENDIF ()

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ENABLE_TESTING()

IF (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # using Clang
    IF (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
        MESSAGE(FATAL_ERROR "Clang version must be greater than 10.0")
    ENDIF ()
ELSEIF (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # using GCC
    IF (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
        MESSAGE(FATAL_ERROR "GCC G++ version must be greater than 11.0")
    ENDIF ()
ENDIF ()

IF(CMAKE_BUILD_TYPE STREQUAL "Release")
    MESSAGE("Building in Release mode")
ELSE()
    MESSAGE("Building in Debug mode")
    OPTION(ADDRESS_SANITIZER "Enable AddressSanitizer (default sanitizer)" OFF)
    OPTION(THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

    IF(THREAD_SANITIZER)
        # enable ThreadSanitizer to detect data race and other thread-related issue.
        SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread -O0 -fno-omit-frame-pointer -fno-optimize-sibling-calls")
        MESSAGE(STATUS "ThreadSanitizer is enabled.")
    ELSEIF(ADDRESS_SANITIZER)
        # enable AddressSanitizer to detect memory leaks and other memory-related bugs.
        SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address  -O0 -fno-omit-frame-pointer -fno-optimize-sibling-calls")
        MESSAGE(STATUS "AddressSanitizer is enabled.")
    ENDIF()
ENDIF()

EXECUTE_PROCESS(COMMAND uname -p OUTPUT_VARIABLE HOST_ARCH)
STRING(TOLOWER ${HOST_ARCH} HOST_ARCH)

IF (NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RELEASE)
ENDIF ()

# NOTE !!! CMAKE_BUILD_TYPE is case-sensitive
STRING(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)

IF (${BUILD_TYPE} STREQUAL DEBUG)
    SET(LIB_BUILD_TYPE DEBUG)
ELSEIF (${BUILD_TYPE} STREQUAL MINSIZEREL)
    SET(LIB_BUILD_TYPE MINSIZEREL)
ELSEIF (${BUILD_TYPE} STREQUAL RELWITHDEBINFO)
    SET(LIB_BUILD_TYPE RELWITHDEBINFO)
ELSE ()
    SET(LIB_BUILD_TYPE RELEASE)
    SET(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
ENDIF ()

IF (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    SET(CMAKE_CXX_FLAGS "-pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++ -lc++ -lc++abi ${CMAKE_EXE_LINKER_FLAGS}")
    ADD_DEFINITIONS(-DOS_MACOSX)
ELSEIF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    IF (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        SET(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++ -fuse-ld=lld -lc++ -lc++abi ${CMAKE_EXE_LINKER_FLAGS}")
        SET(CMAKE_CXX_FLAGS "-stdlib=libc++ -pthread ${CMAKE_CXX_FLAGS}")
        set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    ELSEIF (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        SET(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wl,--no-as-needed -ldl -Wno-restrict")
    ENDIF ()
    ADD_DEFINITIONS(-DOS_LINUX)
ELSE ()
    MESSAGE(FATAL_ERROR "only support linux or macOS")
ENDIF ()

IF (HOST_ARCH MATCHES "x86_64" OR HOST_ARCH MATCHES "i386")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse -msse4.2")
ELSEIF (HOST_ARCH MATCHES "arm")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc+crypto -moutline-atomics")
ENDIF ()

IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    SET(EP_BASE_SUFFIX "build-release")
    SET(LIB_SUFFIX "deps-release")
ELSE ()
    SET(EP_BASE_SUFFIX "build-debug")
    SET(LIB_SUFFIX "deps-debug")
ENDIF ()

SET_PROPERTY(DIRECTORY PROPERTY EP_BASE ${CMAKE_CURRENT_SOURCE_DIR}/${EP_BASE_SUFFIX})
SET(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules/")
SET(BUILD_SUPPORT_DIR ${PROJECT_SOURCE_DIR}/build_support)

SET(PLIB_INSTALL_DIR ${CMAKE_BINARY_DIR}/lib)
SET(LIB_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_SUFFIX})
SET(LIB_INCLUDE_DIR ${LIB_INSTALL_PREFIX}/include)
SET(LIB_INSTALL_DIR ${LIB_INSTALL_PREFIX}/lib)
SET(LIB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${EP_BASE_SUFFIX}/Source)

# make sure we use the same compiler for all dependencies
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

MESSAGE(STATUS "${PROJECT_NAME} staged install: ${STAGED_INSTALL_PREFIX}")
MESSAGE(STATUS "Current platform: ${OS_VERSION} ")
CMAKE_HOST_SYSTEM_INFORMATION(RESULT CPU_CORE QUERY NUMBER_OF_LOGICAL_CORES)
MESSAGE(STATUS "CPU core ${CPU_CORE}")

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

SET(THIRD_PARTY_PATH ${CMAKE_CURRENT_BINARY_DIR}/third-party)
SET(THIRD_PARTY_BUILD_TYPE ${CMAKE_BUILD_TYPE})
SET(EXTERNAL_PROJECT_LOG_ARGS
        LOG_DOWNLOAD 0
        LOG_UPDATE 0
        LOG_CONFIGURE 0
        LOG_BUILD 0
        LOG_TEST 0
        LOG_INSTALL 0
        BUILD_ALWAYS 0
)

IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    SET(LIB rt)
ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    SET(LIB
            pthread
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreData"
            "-framework CoreText"
            "-framework Security"
            "-framework Foundation"
            "-Wl,-U,_MallocExtension_ReleaseFreeMemory"
            "-Wl,-U,_ProfilerStart"
            "-Wl,-U,_ProfilerStop"
            "-Wl,-U,__Z13GetStackTracePPvii")
ENDIF()
SET(LIB ${LIB} CACHE INTERNAL "libs which should be linked for executable target")

INCLUDE(ExternalProject)

INCLUDE(cmake/findTools.cmake)
INCLUDE(cmake/openssl.cmake)
INCLUDE(cmake/lz4.cmake)
INCLUDE(cmake/zstd.cmake)
INCLUDE(cmake/gflags.cmake)
INCLUDE(cmake/snappy.cmake)
INCLUDE(cmake/zlib.cmake)
INCLUDE(cmake/leveldb.cmake)
INCLUDE(cmake/llhttp.cmake)
INCLUDE(cmake/fmt.cmake)
INCLUDE(cmake/spdlog.cmake)
INCLUDE(cmake/gtest.cmake)
INCLUDE(cmake/rocksdb.cmake)
INCLUDE(cmake/protobuf.cmake)
INCLUDE(cmake/brpc.cmake)
INCLUDE(cmake/braft.cmake)

SET(PROTO_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated_pb")
FILE(MAKE_DIRECTORY "${PROTO_OUTPUT_DIR}")

#INCLUDE(cmake/protogen.cmake)

ADD_SUBDIRECTORY(src/pstd)
ADD_SUBDIRECTORY(src/net)
ADD_SUBDIRECTORY(src/praft)
ADD_SUBDIRECTORY(src/storage)

ADD_SUBDIRECTORY(src)

#############################################################################
###			custom target
#############################################################################
# Add files which should be ignored while formatting
LIST(APPEND CLANG_FORMAT_IGNORE_FILES "${PROJECT_SOURCE_DIR}/src/storage/src/storage_murmur3.h")
FILE(WRITE ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt "")
FOREACH(IGNORE_FILE ${CLANG_FORMAT_IGNORE_FILES})
  FILE(APPEND ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt "${IGNORE_FILE}\n")
ENDFOREACH()

STRING(CONCAT FORMAT_DIRS "${PROJECT_SOURCE_DIR}/src,")
ADD_CUSTOM_TARGET(format
  COMMAND ${BUILD_SUPPORT_DIR}/run_clang_format.py
  ${CLANG_FORMAT_BIN}
  ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt
  --source_dirs ${FORMAT_DIRS}
  --quiet
  --fix
)
ADD_CUSTOM_TARGET(check-format
  COMMAND ${BUILD_SUPPORT_DIR}/run_clang_format.py
  ${CLANG_FORMAT_BIN}
  ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt
  --source_dirs ${FORMAT_DIRS}
)

ADD_CUSTOM_TARGET(clang-tidy
  COMMAND ${BUILD_SUPPORT_DIR}/run_clang_tidy.py
  -clang-tidy-binary ${CLANG_TIDY_BIN}
  -p ${CMAKE_BINARY_DIR}
  -extra-arg=-std=c++17
)

ADD_CUSTOM_TARGET(clang-tidy-fix 
  COMMAND ${BUILD_SUPPORT_DIR}/run_clang_tidy.py
  -clang-tidy-binary ${CLANG_TIDY_BIN}
  -p ${CMAKE_BINARY_DIR}
  -extra-arg=-std=c++17
  -fix
)

FILE(GLOB_RECURSE LINT_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)
ADD_CUSTOM_TARGET(cpplint echo '${LINT_FILES}' | xargs -n12 -P8
  ${CPPLINT_BIN}
  --verbose=2 --quiet
  --linelength=120
  --filter=-legal/copyright,-build/header_guard,-runtime/references
)
