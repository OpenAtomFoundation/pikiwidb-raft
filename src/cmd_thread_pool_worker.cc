/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_thread_pool_worker.h"
#include "log.h"
#include "pikiwidb.h"

namespace pikiwidb {

void CmdWorkThreadPoolWorker::Work() {
  while (running_) {
    LoadWork();
    for (const auto &task : self_task_) {
      if (task->Client()->State() != ClientState::kOK) {  // the client is closed
        continue;
      }
      auto [cmdPtr, ret] = cmd_table_manager_.GetCommand(task->CmdName(), task->Client().get());

      if (!cmdPtr) {
        if (ret == CmdRes::kUnknownCmd) {
          task->Client()->SetRes(CmdRes::kErrOther, "unknown command '" + task->CmdName() + "'");
        } else if (ret == CmdRes::kUnknownSubCmd) {
          task->Client()->SetRes(CmdRes::kErrOther, "unknown sub command '" + task->Client().get()->argv_[1] + "'");
        } else {
          task->Client()->SetRes(CmdRes::kInvalidParameter);
        }
        g_pikiwidb->PushWriteTask(task->Client());
        continue;
      }

      if (!cmdPtr->CheckArg(task->Client()->ParamsSize())) {
        task->Client()->SetRes(CmdRes::kWrongNum, task->CmdName());
        g_pikiwidb->PushWriteTask(task->Client());
        continue;
      }
      task->Run(cmdPtr);
      g_pikiwidb->PushWriteTask(task->Client());
    }
    self_task_.clear();
  }
  INFO("worker [{}] goodbye...", name_);
}

void CmdWorkThreadPoolWorker::Stop() { running_ = false; }

void CmdFastWorker::LoadWork() {
  std::unique_lock lock(pool_->fast_mutex_);
  while (pool_->fast_tasks_.empty()) {
    if (!running_) {
      return;
    }
    pool_->fast_condition_.wait(lock);
  }

  if (pool_->fast_tasks_.empty()) {
    return;
  }
  const auto num = std::min(static_cast<int>(pool_->fast_tasks_.size()), once_task_);
  std::move(pool_->fast_tasks_.begin(), pool_->fast_tasks_.begin() + num, std::back_inserter(self_task_));
  pool_->fast_tasks_.erase(pool_->fast_tasks_.begin(), pool_->fast_tasks_.begin() + num);
}

void CmdSlowWorker::LoadWork() {
  {
    std::unique_lock lock(pool_->slow_mutex_);
    while (pool_->slow_tasks_.empty() && loop_more_) {  // loopMore is used to get the fast worker
      if (!running_) {
        return;
      }
      pool_->slow_condition_.wait_for(lock, std::chrono::milliseconds(wait_time_));
      loop_more_ = false;
    }

    const auto num = std::min(static_cast<int>(pool_->slow_tasks_.size()), once_task_);
    if (num > 0) {
      std::move(pool_->slow_tasks_.begin(), pool_->slow_tasks_.begin() + num, std::back_inserter(self_task_));
      pool_->slow_tasks_.erase(pool_->slow_tasks_.begin(), pool_->slow_tasks_.begin() + num);
      return;  // If the slow task is obtained, the fast task is no longer obtained
    }
  }

  {
    std::unique_lock lock(pool_->fast_mutex_);
    loop_more_ = true;

    const auto num = std::min(static_cast<int>(pool_->fast_tasks_.size()), once_task_);
    if (num > 0) {
      std::move(pool_->fast_tasks_.begin(), pool_->fast_tasks_.begin() + num, std::back_inserter(self_task_));
      pool_->fast_tasks_.erase(pool_->fast_tasks_.begin(), pool_->fast_tasks_.begin() + num);
    }
  }
}

}  // namespace pikiwidb
