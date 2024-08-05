/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <shared_mutex>
#include <thread>

#include "timer_task.h"

namespace net {

class Timer {
 public:
  explicit Timer(int64_t interval) : interval_(interval) {}

  ~Timer() = default;

 private:
  std::shared_mutex lock_;
  std::shared_mutex lockSet_;
  int64_t interval_;
  int64_t taskId_ = 0;
  std::set<int64_t> markDel;
  std::priority_queue<std::shared_ptr<ITimerTask>> queue_;
  std::vector<std::shared_ptr<ITimerTask>> list_;

 public:
  inline int64_t Interval() const { return interval_; }

  int64_t AddTask(const std::shared_ptr<ITimerTask>& task);

  inline void DelTask(int64_t taskId) {
    std::unique_lock l(lockSet_);
    markDel.insert(taskId);
  }

  void OnTimer();

 private:
  inline int64_t TaskId() { return ++taskId_; }

  inline bool Deleted(int64_t taskId) {
    std::shared_lock l(lockSet_);
    return markDel.count(taskId);
  }

  inline void DelMark(int64_t taskId) {
    std::unique_lock l(lockSet_);
    markDel.erase(taskId);
  }

  void RePushTask();

  void PopTask(std::shared_ptr<ITimerTask>& task, bool deleted);
};

}  // namespace net
