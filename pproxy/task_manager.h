/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <string>
#include <type_traits>

#include "pstd/thread_pool.h"

class Task {
 public:
  Task();
  Task(const Task&);
  Task& operator=(const Task&);
  bool operator==(const Task&);
  Task(const std::string&);
  ~Task();

  enum Status { pending, completed, deleted, recurring, waiting };

  static Status TextToStatus(const std::string& input);
  static std::string StatusToText(Status s);

  void SetEntry();

  Status GetStatus() const;
  void SetStatus(Status);

 private:
  int determineVersion(const std::string&);
  void legacyParse(const std::string&);
  int id;
};

class TaskManager {
 public:
  TaskManager(std::shared_ptr<Threadpool> threadpool, size_t maxWorkers);

  std::future<void> Stop();

  template <class F, class... Args>
  auto Push(F&& function, Args&&... args)  //
      -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(  //
        std::bind(std::forward<F>(function), std::forward<Args>(args)...));
    auto future = task->get_future();

    auto functor = [this, task = std::move(task)]() mutable {
      (*task)();
      {
        std::lock_guard<std::mutex> guard(mutex_);

        --workerCount_;
        this->processTasks();
      }
    };
    this->addTask(std::move(functor));

    return future;
  }

 private:
  void addTask(std::function<void()> functor);
  void processTasks();

 private:
  std::shared_ptr<Threadpool> threadpool_;
  std::queue<Task> tasks_;
  std::mutex mutex_;
  size_t maxWorkers_;
  size_t workerCount_{0};
  bool stopped_{false};
};