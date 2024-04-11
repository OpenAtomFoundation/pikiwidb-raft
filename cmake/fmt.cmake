# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

INCLUDE_GUARD()

FetchContent_Declare(fmt
        URL https://github.com/fmtlib/fmt/archive/10.1.1.zip
        URL_HASH SHA256=3c2e73019178ad72b0614a3124f25de454b9ca3a1afe81d5447b8d3cbdb6d322
        )

FetchContent_MakeAvailable(fmt)
