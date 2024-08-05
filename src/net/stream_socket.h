/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

#include "base_socket.h"

namespace net {

class StreamSocket : public BaseSocket {
 public:
  StreamSocket(int fd, int type) : BaseSocket(fd) { SetSocketType(type); }

  int Init() override { return 1; };

  int OnReadable(const std::shared_ptr<Connection> &conn, std::string *readBuff) override;

  int OnWritable() override;

  bool SendPacket(std::string &&msg) override;

  int Read(std::string *readBuff);

 private:
  const int readBuffSize_ = 4 * 1024;  // read from socket buff size 4K

  std::mutex sendMutex_;  // send data buff mutex

  std::string sendData_;  // send data buff
  size_t sendPos_ = 0;    // send data buff pos
};

}  // namespace net
