/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <thread>

#include "base_event.h"

namespace net {

class IOThread {
 public:
  explicit IOThread(const std::shared_ptr<BaseEvent> &event) : baseEvent_(event){};

  ~IOThread() = default;

  // Initialize the event and run the event loop
  bool Run();

  inline void CloseConnection(int fd) { baseEvent_->DelEvent(fd); }

  // Stop the event loop and wait for the thread to exit
  void Stop();

  // Wait for the thread to exit
  void Wait();

  // Add read event to epoll when send message to client
  inline void SetWriteEvent(uint64_t id, int fd) { baseEvent_->AddWriteEvent(id, fd); }

  // Add new event to epoll when new connection
  inline void AddNewEvent(uint64_t connId, int fd, int mask) { baseEvent_->AddEvent(connId, fd, mask); }

 protected:
  std::atomic<bool> running_ = true;

  std::thread thread_;

  std::shared_ptr<BaseEvent> baseEvent_;  // Event object
};

}  // namespace net
