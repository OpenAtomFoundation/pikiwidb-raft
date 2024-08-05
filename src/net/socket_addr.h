/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <string>

namespace net {

struct SocketAddr {
  SocketAddr() { Clear(); }

  SocketAddr(const SocketAddr &other) { memcpy(&addr_, &other.addr_, sizeof addr_); }

  SocketAddr &operator=(const SocketAddr &other) {
    if (this != &other) {
      memcpy(&addr_, &other.addr_, sizeof addr_);
    }
    return *this;
  }

  explicit SocketAddr(const sockaddr_in &addr) { Init(addr); }

  SocketAddr(uint32_t netip, uint16_t netport) { Init(netip, netport); }

  SocketAddr(const std::string &ip, uint16_t hostport) { Init(ip, hostport); }

  void Init(const sockaddr_in &addr) { memcpy(&addr_, &addr, sizeof(addr)); }

  void Init(uint32_t netIp, uint16_t netPort) {
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = netIp;
    addr_.sin_port = netPort;
  }

  void Init(const std::string &ip, uint16_t hostPort) {
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = ::inet_addr(ip.data());
    addr_.sin_port = htons(hostPort);
  }

  const sockaddr_in &GetAddr() const { return addr_; }

  inline std::string GetIP() const { return ::inet_ntoa(addr_.sin_addr); }

  inline std::string GetIP(char *buf, socklen_t size) const {
    return ::inet_ntop(AF_INET, reinterpret_cast<const char *>(&addr_.sin_addr), buf, size);
  }

  inline uint16_t GetPort() const { return ntohs(addr_.sin_port); }

  inline bool IsValid() const { return 0 != addr_.sin_family; }

  void Clear() { memset(&addr_, 0, sizeof addr_); }

  inline friend bool operator==(const SocketAddr &a, const SocketAddr &b) {
    return a.addr_.sin_family == b.addr_.sin_family && a.addr_.sin_addr.s_addr == b.addr_.sin_addr.s_addr &&
           a.addr_.sin_port == b.addr_.sin_port;
  }

  inline friend bool operator!=(const SocketAddr &a, const SocketAddr &b) { return !(a == b); }

  sockaddr_in addr_{};
};

}  // namespace net
