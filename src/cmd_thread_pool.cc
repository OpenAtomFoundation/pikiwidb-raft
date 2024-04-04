/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_thread_pool.h"
#include "cmd_thread_pool_worker.h"
#include "log.h"

namespace pikiwidb {

void CmdThreadPoolTask::Run(BaseCmd *cmd) { cmd->Execute(client_.get()); }
const std::string &CmdThreadPoolTask::CmdName() { return client_->CmdName(); }
std::shared_ptr<PClient> CmdThreadPoolTask::Client() { return client_; }

CmdThreadPool::CmdThreadPool(std::string name) : name_(std::move(name)) {}

pstd::Status CmdThreadPool::Init(int fast_thread, int slow_thread, std::string name) {
  if (fast_thread <= 0) {
    return pstd::Status::InvalidArgument("thread num must be positive");
  }
  name_ = std::move(name);
  fast_thread_num_ = fast_thread;
  slow_thread_num_ = slow_thread;
  threads_.reserve(fast_thread_num_ + slow_thread_num_);
  workers_.reserve(fast_thread_num_ + slow_thread_num_);
  return pstd::Status::OK();
}

void CmdThreadPool::Start() {
  for (int i = 0; i < fast_thread_num_; ++i) {
    auto fastWorker = std::make_shared<CmdFastWorker>(this, 2, "fast worker" + std::to_string(i));
    std::thread thread(&CmdWorkThreadPoolWorker::Work, fastWorker);
    threads_.emplace_back(std::move(thread));
    workers_.emplace_back(fastWorker);
    INFO("fast worker [{}] starting ...", i);
  }
  for (int i = 0; i < slow_thread_num_; ++i) {
    auto slowWorker = std::make_shared<CmdSlowWorker>(this, 2, "slow worker" + std::to_string(i));
    std::thread thread(&CmdWorkThreadPoolWorker::Work, slowWorker);
    threads_.emplace_back(std::move(thread));
    workers_.emplace_back(slowWorker);
    INFO("slow worker [{}] starting ...", i);
  }
}

void CmdThreadPool::SubmitFast(const std::shared_ptr<CmdThreadPoolTask> &runner) {
  std::unique_lock rl(fast_mutex_);
  fast_tasks_.emplace_back(runner);
  fast_condition_.notify_one();
}

void CmdThreadPool::SubmitSlow(const std::shared_ptr<CmdThreadPoolTask> &runner) {
  std::unique_lock rl(slow_mutex_);
  slow_tasks_.emplace_back(runner);
  slow_condition_.notify_one();
}

void CmdThreadPool::Stop() { DoStop(); }

void CmdThreadPool::DoStop() {
  if (stopped_.load()) {
    return;
  }
  stopped_.store(true);

  for (auto &worker : workers_) {
    worker->Stop();
  }

  {
    std::unique_lock fl(fast_mutex_);
    fast_condition_.notify_all();
  }
  {
    std::unique_lock sl(slow_mutex_);
    slow_condition_.notify_all();
  }

  for (auto &thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();
  workers_.clear();
  fast_tasks_.clear();
  slow_tasks_.clear();
}

CmdThreadPool::~CmdThreadPool() { DoStop(); }

}  // namespace pikiwidb
