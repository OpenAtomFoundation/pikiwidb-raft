/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <sys/socket.h>

#include <functional>
#include <string>

#include "base_event.h"
#include "net_event.h"
#include "socket_addr.h"

namespace net {

static constexpr int SOCKET_WIN_SIZE = 128 * 1024;

class BaseSocket : public NetEvent {
 public:
  enum {
    SOCKET_NONE = 0,  // error socket
    SOCKET_TCP,
    SOCKET_UDP,
    SOCKET_LISTEN_TCP,
    SOCKET_LISTEN_UDP,
  };

  explicit BaseSocket(int fd) : NetEvent(fd) {}

  ~BaseSocket() override = default;

  void OnError() override{};

  void Close() override;

  static int CreateTCPSocket();

  static int CreateUDPSocket();

  // Called when the socket is created
  void OnCreate();

  void SetNonBlock(bool noBlock);

  void SetNodelay();

  void SetSndBuf(socklen_t size = SOCKET_WIN_SIZE);

  void SetRcvBuf(socklen_t size = SOCKET_WIN_SIZE);

  void SetReuseAddr();

  bool SetReusePort();

  bool GetLocalAddr(SocketAddr &);

  bool GetPeerAddr(SocketAddr &);

  inline int SocketType() { return type_; }

  inline void SetSocketType(int type) { type_ = type; }

 protected:
  inline bool NoBlock() const { return noBlock_; }

 private:
  int type_ = SOCKET_NONE;  // socket type (TCP/UDP)
  bool noBlock_ = true;
};

}  // namespace net
