/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "event_server.h"

namespace net {

std::atomic<uint64_t> g_connId = 0;

uint64_t getConnId() { return ++g_connId; }

}  // namespace net
