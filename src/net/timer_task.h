/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma

#include <chrono>
#include <cstdint>
#include <functional>

namespace net {

class ITimerTask {
 public:
  explicit ITimerTask(int64_t interval) : ITimerTask(0, interval) {}

  ITimerTask(int64_t start, int64_t interval) : interval_(interval) {
    if (start <= 0) {
      start = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                  .count();
    }
    start_ = start;
  }

  virtual void TimeOut() = 0;

  inline int64_t Start() const { return start_; }

  inline int64_t Interval() const { return interval_; }

  inline void Next() {  // the next time it can be executed
    start_ += Interval();
  }

  inline int64_t Id() const { return id_; }

  inline void SetId(int64_t id) { id_ = id; }

  inline bool operator<(ITimerTask& task1) const { return task1.Start() < Start(); }

 protected:
  int64_t start_ = 0;     // Timestamp of the start of the task (in milliseconds)
  int64_t interval_ = 0;  // Task interval (ms)
  int64_t id_ = 0;
};

class CommonTimerTask : public ITimerTask {
 public:
  explicit CommonTimerTask(int64_t interval) : ITimerTask(interval) {}

  CommonTimerTask(int64_t start, int64_t interval) : ITimerTask(start, interval) {}

  void TimeOut() override { callback_(); }

  template <typename F, typename... Args>
  void SetCallback(F&& f, Args&&... args) {
    auto temp = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    callback_ = [temp]() { (void)temp(); };
  }

 private:
  std::function<void()> callback_;
};

}  // namespace net
