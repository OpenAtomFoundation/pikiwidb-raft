/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <utility>
#include <vector>
#include "base_cmd.h"
#include "pstd/pstd_status.h"

namespace pikiwidb {

// task interface
// inherit this class and implement the Run method
// then submit the task to the thread pool
class CmdThreadPoolTask {
 public:
  explicit CmdThreadPoolTask(std::shared_ptr<PClient> client) : client_(std::move(client)) {}
  void Run(BaseCmd *cmd);
  const std::string &CmdName();
  std::shared_ptr<PClient> Client();

 private:
  std::shared_ptr<PClient> client_;
};

class CmdWorkThreadPoolWorker;

class CmdFastWorker;

class CmdSlowWorker;

class CmdThreadPool {
  friend CmdWorkThreadPoolWorker;
  friend CmdFastWorker;
  friend CmdSlowWorker;

 public:
  explicit CmdThreadPool() = default;

  explicit CmdThreadPool(std::string name);

  pstd::Status Init(int fast_thread, int slow_thread, std::string name);

  // start the thread pool
  void Start();

  // stop the thread pool
  void Stop();

  // submit a fast task to the thread pool
  void SubmitFast(const std::shared_ptr<CmdThreadPoolTask> &runner);

  // submit a slow task to the thread pool
  void SubmitSlow(const std::shared_ptr<CmdThreadPoolTask> &runner);

  // get the fast thread num
  inline int FastThreadNum() const { return fast_thread_num_; };

  // get the slow thread num
  inline int SlowThreadNum() const { return slow_thread_num_; };

  // get the thread pool size
  inline int ThreadPollSize() const { return fast_thread_num_ + slow_thread_num_; };

  ~CmdThreadPool();

 private:
  void DoStop();

 private:
  std::deque<std::shared_ptr<CmdThreadPoolTask>> fast_tasks_;  // fast task queue
  std::deque<std::shared_ptr<CmdThreadPoolTask>> slow_tasks_;  // slow task queue

  std::vector<std::thread> threads_;
  std::vector<std::shared_ptr<CmdWorkThreadPoolWorker>> workers_;
  std::string name_;  // thread pool name
  int fast_thread_num_ = 0;
  int slow_thread_num_ = 0;
  std::mutex fast_mutex_;
  std::condition_variable fast_condition_;
  std::mutex slow_mutex_;
  std::condition_variable slow_condition_;
  std::atomic_bool stopped_ = false;
};

}  // namespace pikiwidb
