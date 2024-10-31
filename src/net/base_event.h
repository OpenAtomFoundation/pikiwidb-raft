/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <unistd.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <latch>
#include <map>
#include <memory>
#include <utility>

#include "callback_function.h"
#include "net_event.h"
#include "timer.h"

namespace net {

class BaseEvent : public std::enable_shared_from_this<BaseEvent> {
 public:
  // Currently, there are two types of multiplexing: epoll and kqueue
  enum {
    EVENT_TYPE_EPOLL = 1,
    EVENT_TYPE_KQUEUE,
  };

  // Whether to enable read/write separation. If read/write separation is enabled,
  // read and write are in different multiplexes.
  // If not, write events need to be processed in read multiplexes
  enum {
    EVENT_MODE_READ = (1 << 0),   // only read
    EVENT_MODE_WRITE = (1 << 1),  // only write
  };

  // multiplexes event
  const static int EVENT_READ;
  const static int EVENT_WRITE;
  const static int EVENT_ERROR;
  const static int EVENT_HUB;

  BaseEvent(const std::shared_ptr<NetEvent> &listen, int8_t mode, int8_t type)
      : listen_(listen), mode_(mode), type_(type){};

  virtual ~BaseEvent() = default;

  // add fd to poll
  virtual void AddEvent(uint64_t id, int fd, int mask) = 0;

  // delete fd from poll
  virtual void DelEvent(int fd) = 0;

  // add write event
  virtual void AddWriteEvent(uint64_t id, int fd) = 0;

  // delete write event
  virtual void DelWriteEvent(uint64_t id, int fd) = 0;

  // poll event
  virtual void EventPoll() = 0;

  // init the multiplexes
  virtual bool Init() = 0;

  void AddTimer(const std::shared_ptr<Timer> &timer) { timer_ = timer; };

  void Close() {
    bool run = true;
    if (running_.compare_exchange_strong(run, false)) {
      char signal_byte = 'X';
      ::write(pipeFd_[1], &signal_byte, sizeof(signal_byte));  // send signal to pipe，end poll loop
      close(EvFd());
    }
  }

  inline int EvFd() const { return evFd_; }

  inline void SetOnCreate(std::function<void(uint64_t, std::shared_ptr<Connection>)> &&onCreate) {
    onCreate_ = std::move(onCreate);
  }

  inline void SetOnMessage(std::function<void(uint64_t, std::string &&)> &&onMessage) {
    onMessage_ = std::move(onMessage);
  }

  inline void SetOnClose(std::function<void(uint64_t, std::string &&)> &&onClose) { onClose_ = std::move(onClose); }

  inline void SetGetConn(std::function<std::shared_ptr<Connection>(uint64_t)> &&getConn) {
    getConn_ = std::move(getConn);
  }

  inline int8_t Type() const { return type_; }

 protected:
  int evFd_ = 0;  // event fd
  std::atomic<bool> running_ = true;

  // Type of multiplexing supported.
  // If read/write fractions are not enabled,
  // write events must be processed simultaneously in the read multiplexing
  const int8_t mode_ = 0;

  // The type of the current multiplexing is epoll or kqueue
  const int8_t type_ = 0;

  int pipeFd_[2]{};

  std::shared_ptr<Timer> timer_;

  // listening socket
  std::shared_ptr<NetEvent> listen_;

  // callback function when a new connection is created
  std::function<void(uint64_t, std::shared_ptr<Connection>)> onCreate_;

  // callback function when a message is received
  std::function<void(uint64_t, std::string &&)> onMessage_;

  // callback function when a connection is closed
  std::function<void(uint64_t, std::string &&)> onClose_;

  // get connection by connID
  std::function<std::shared_ptr<Connection>(uint64_t)> getConn_;
};

}  // namespace net
