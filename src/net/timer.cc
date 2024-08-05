/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "timer.h"

namespace net {

int64_t Timer::AddTask(const std::shared_ptr<ITimerTask>& task) {
  if (!task) {
    return -1;
  }
  std::unique_lock l(lock_);
  int64_t _taskId = TaskId();
  task->SetId(_taskId);
  queue_.push(task);
  return _taskId;
}

void Timer::RePushTask() {
  for (const auto& task : list_) {
    task->Next();
    { queue_.push(task); }
  }
  list_.clear();
}

void Timer::PopTask(std::shared_ptr<ITimerTask>& task, bool deleted) {
  if (!task) {
    return;
  }
  if (deleted) {
    DelMark(task->Id());
  } else {
    list_.emplace_back(task);
  }

  queue_.pop();
}

void Timer::OnTimer() {
  std::unique_lock l(lock_);
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                 .count();

  while (!queue_.empty()) {
    auto task = queue_.top();
    if (Deleted(task->Id())) {
      PopTask(task, true);
      continue;
    }
    if (now >= task->Start()) {
      task->TimeOut();
      PopTask(task, false);
    } else {
      break;
    }
  }

  RePushTask();  // reload the task
}

}  // namespace net
