/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>

#include "callback_function.h"

namespace net {

// For human readability
enum {
  NE_ERROR = -1,
  NE_CLOSE = 0,
  NE_OK = 1,
};

enum class NetListen {
  OK = 0,
  OPEN_ERROR,
  BIND_ERROR,
  LISTEN_ERROR,
};

// abstraction of all networks
class NetEvent {
 public:
  explicit NetEvent(int fd) : fd_(fd) {}

  virtual ~NetEvent() = default;

  // Initialize the event
  virtual int Init() = 0;

  // Handle read event when the connection is readable and the data can be read
  virtual int OnReadable(const std::shared_ptr<Connection> &conn, std::string *readBuff) = 0;

  // Handle write event when the connection is writable and the data can be sent
  virtual int OnWritable() = 0;

  virtual void OnError() = 0;

  // Send data
  virtual bool SendPacket(std::string &&msg) = 0;

  virtual void Close() = 0;

  inline int Fd() const { return fd_.load(); }

 protected:
  std::atomic<int> fd_ = 0;
};

}  // namespace net
