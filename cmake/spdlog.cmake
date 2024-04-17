# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

INCLUDE_GUARD()

FetchContent_Declare(spdlog
        URL https://github.com/gabime/spdlog/archive/v1.12.0.zip
        URL_HASH SHA256=6174BF8885287422A6C6A0312EB8A30E8D22BCFCEE7C48A6D02D1835D7769232
        )

SET(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules/spdlog" CACHE STRING "" FORCE)
SET(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)
