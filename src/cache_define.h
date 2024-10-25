// Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

namespace pikiwidb {
/*
 * cache mode
 */
constexpr int PCACHE_NONE = 0;
constexpr int PCACHE_READ = 1;

/*
 * cache status
 */
const int PCACHE_STATUS_NONE = 0;
const int PCACHE_STATUS_INIT = 1;
const int PCACHE_STATUS_OK = 2;
const int PCACHE_STATUS_RESET = 3;
const int PCACHE_STATUS_DESTROY = 4;
const int PCACHE_STATUS_CLEAR = 5;
const int PCACHE_START_FROM_BEGIN = 0;
const int PCACHE_START_FROM_END = -1;

/*
 * key type
 */
const char KEY_TYPE_KV = 'k';
const char KEY_TYPE_HASH = 'h';
const char KEY_TYPE_LIST = 'l';
const char KEY_TYPE_SET = 's';
const char KEY_TYPE_ZSET = 'z';

const int64_t CACHE_LOAD_QUEUE_MAX_SIZE = 2048;
const int64_t CACHE_VALUE_ITEM_MAX_SIZE = 2048;
const int64_t CACHE_LOAD_NUM_ONE_TIME = 256;

// TTL option
const int PCache_TTL_NONE = -1;
const int PCache_KEY_NOT_FOUND = -2;
const int PCache_TTL_FAILED = -3;
}  // namespace pikiwidb
