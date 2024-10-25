/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "client.h"
#include "pcache.h"
#include "thread.h"

namespace pikiwidb {

class PCacheLoadThread : public Thread {
 public:
  PCacheLoadThread(int zset_cache_start_direction, int zset_cache_field_num_per_key);
  ~PCacheLoadThread() override;

  uint64_t AsyncLoadKeysNum(void) { return async_load_keys_num_; }
  uint32_t WaittingLoadKeysNum(void) { return waitting_load_keys_num_; }
  void Push(const char key_type, std::string& key, PClient* client);

 private:
  bool LoadKV(std::string& key, PClient* client);
  bool LoadHash(std::string& key, PClient* client);
  bool LoadList(std::string& key, PClient* client);
  bool LoadSet(std::string& key, PClient* client);
  bool LoadZset(std::string& key, PClient* client);
  bool LoadKey(const char key_type, std::string& key, PClient* client);
  virtual void* ThreadMain() override;

 private:
  std::atomic_bool should_exit_;
  std::deque<std::tuple<const char, std::string, PClient*>> loadkeys_queue_;

  pstd::CondVar loadkeys_cond_;
  pstd::Mutex loadkeys_mutex_;

  std::unordered_map<std::string, std::string> loadkeys_map_;
  pstd::Mutex loadkeys_map_mutex_;
  std::atomic_uint64_t async_load_keys_num_;
  std::atomic_uint32_t waitting_load_keys_num_;
  std::shared_ptr<PCache> cache_;
  // currently only take effects to zset
  int zset_cache_start_direction_;
  int zset_cache_field_num_per_key_;
};
}  // namespace pikiwidb
