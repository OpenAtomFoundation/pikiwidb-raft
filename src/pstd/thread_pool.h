/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace pstd {

class ThreadPool final {
 public:
  ThreadPool();
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  void operator=(const ThreadPool&) = delete;

  template <typename F, typename... Args>
  auto ExecuteTask(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

  void JoinAll();
  void SetMaxIdleThread(unsigned int m);

 private:
  void CreateWorker();
  void WorkerRoutine();
  void MonitorRoutine();

  std::thread monitor_;
  std::atomic<unsigned> maxIdleThread_;
  std::atomic<unsigned> pendingStopSignal_;

  static thread_local bool working_;
  std::deque<std::thread> worker_threads_;

  std::mutex mutex_;
  std::condition_variable cond_;
  unsigned waiters_;
  bool shutdown_;
  std::deque<std::function<void()>> tasks_;

  static const int kMaxThreads = 256;
};

template <typename F, typename... Args>
auto ThreadPool::ExecuteTask(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
  using resultType = std::invoke_result_t<F, Args...>;

  auto task =
      std::make_shared<std::packaged_task<resultType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  {
    std::unique_lock<std::mutex> guard(mutex_);
    if (shutdown_) {
      return std::future<resultType>();
    }

    tasks_.emplace_back([=]() { (*task)(); });
    if (waiters_ == 0) {
      CreateWorker();
    }

    cond_.notify_one();
  }

  return task->get_future();
}

}  // namespace pstd
