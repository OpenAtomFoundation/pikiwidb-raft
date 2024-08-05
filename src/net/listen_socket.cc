/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <netinet/tcp.h>

#include "config.h"
#include "listen_socket.h"
#include "log.h"
#include "stream_socket.h"

namespace net {

const int ListenSocket::LISTENQ = 1024;

bool ListenSocket::REUSE_PORT = true;

int ListenSocket::OnReadable(const std::shared_ptr<Connection> &conn, std::string *readBuff) {
  struct sockaddr_in clientAddr {};
  auto newConnFd = Accept(&clientAddr);
  if (newConnFd == 0) {
    ERROR("ListenSocket fd:{},Accept error:{}", Fd(), errno);
    return NE_ERROR;
  }

  auto newConn = std::make_unique<StreamSocket>(newConnFd, SocketType());

  char clientIP[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);

  newConn->OnCreate();
  conn->netEvent_ = std::move(newConn);
  conn->fd_ = newConnFd;
  conn->addr_.Init(clientAddr);

  return newConnFd;
}

int ListenSocket::OnWritable() { return 1; }

bool ListenSocket::SendPacket(std::string &&msg) { return false; }

int ListenSocket::Init() {
  if (!Open()) {
    return static_cast<int>(NetListen::OPEN_ERROR);
  }
  if (!Bind()) {
    return static_cast<int>(NetListen::BIND_ERROR);
  }

  if (!Listen()) {
    return static_cast<int>(NetListen::LISTEN_ERROR);
  }
  return static_cast<int>(NetListen::OK);
}

bool ListenSocket::Open() {
  if (Fd() != 0) {
    return false;
  }

  if (!addr_.IsValid()) {
    ERROR("ListenSocket addr IP:{}, PORT:{} is invalid", addr_.GetIP(), addr_.GetPort());
    return false;
  }

  if (SocketType() == SOCKET_LISTEN_TCP) {
    fd_ = CreateTCPSocket();
  } else if (SocketType() == SOCKET_LISTEN_UDP) {
    fd_ = CreateUDPSocket();
  } else {
    return false;
  }
  return true;
}

bool ListenSocket::Bind() {
  if (Fd() <= 0) {
    return false;
  }

  SetNonBlock(true);
  SetNodelay();
  SetReuseAddr();
  if (!SetReusePort()) {
    REUSE_PORT = false;
  }

  struct sockaddr_in serv = addr_.GetAddr();

  int ret = ::bind(Fd(), reinterpret_cast<struct sockaddr *>(&serv), sizeof serv);
  if (0 != ret) {
    ERROR("ListenSocket fd:{},Bind error:{}", Fd(), errno);
    Close();
    return false;
  }
  return true;
}

bool ListenSocket::Listen() {
  int ret = ::listen(Fd(), ListenSocket::LISTENQ);
  if (0 != ret) {
    ERROR("ListenSocket fd:{},Listen error:{}", Fd(), errno);
    Close();
    return false;
  }
  return true;
}

int ListenSocket::Accept(sockaddr_in *clientAddr) {
  socklen_t addrLength = sizeof(*clientAddr);
#ifdef HAVE_ACCEPT4
  return ::accept4(Fd(), reinterpret_cast<struct sockaddr *>(clientAddr), &addrLength, SOCK_NONBLOCK);
#else
  return ::accept(Fd(), reinterpret_cast<struct sockaddr *>(clientAddr), &addrLength);
#endif
}

}  // namespace net
