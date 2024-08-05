/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "config.h"

#ifdef HAVE_EPOLL

#  include <fcntl.h>
#  include <sys/epoll.h>
#  include <sys/socket.h>
#  include <utility>

#  include "base_event.h"

namespace net {

class EpollEvent : public BaseEvent {
 public:
  explicit EpollEvent(const std::shared_ptr<NetEvent> &listen, int8_t mode)
      : BaseEvent(listen, mode, BaseEvent::EVENT_TYPE_EPOLL){};

  ~EpollEvent() override { Close(); }

  // Initialize the epoll event
  bool Init() override;

  // Add event to epoll, mask is the event type
  void AddEvent(uint64_t id, int fd, int mask) override;

  // Delete event from epoll
  void DelEvent(int fd) override;

  // Poll event
  void EventPoll() override;

  // Add write event to epoll
  void AddWriteEvent(uint64_t id, int fd) override;

  // Delete write event from epoll
  void DelWriteEvent(uint64_t id, int fd) override;

  // Handle read event
  void EventRead();

  // Handle write event
  void EventWrite();

  // Do read event
  void DoRead(const epoll_event &event, const std::shared_ptr<Connection> &conn);

  // Do write event
  void DoWrite(const epoll_event &event, const std::shared_ptr<Connection> &conn);

  // Handle error event
  void DoError(const epoll_event &event, std::string &&err);

 private:
  const int eventsSize = 1024;
};

}  // namespace net
#endif
