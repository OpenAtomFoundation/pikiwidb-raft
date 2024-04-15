/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <future>
#include <shared_mutex>
#include <vector>

namespace pikiwidb {

class DB;

class CheckpointManager {
 public:
  CheckpointManager() = default;
  ~CheckpointManager() = default;

  void Init(int instNum, DB* db);

  void CreateCheckpoint(const std::string& path);

  void WaitForCheckpointDone();

 private:
  int checkpoint_num_ = 0;
  std::vector<std::future<void>> res_;
  DB* db_ = nullptr;

  std::shared_mutex shared_mutex_;
};

}  // namespace pikiwidb
