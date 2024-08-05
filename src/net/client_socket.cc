/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <sstream>

#include "client_socket.h"

namespace net {

bool ClientSocket::Connect() {
  fd_ = CreateTCPSocket();
  if (fd_ == -1) {
    onConnectFail_("CreateTCPSocket open socket failed");
    return false;
  }
  SetNonBlock(true);
  SetNodelay();
  SetRcvBuf();
  SetSndBuf();

  auto ret = connect(Fd(), (sockaddr*)&addr_.GetAddr(), sizeof(sockaddr_in));
  if (0 != ret) {
    if (EINPROGRESS == errno) {
      return true;
    }

    std::ostringstream oss;
    oss << "IP:" << addr_.GetIP() << " port:" << addr_.GetPort() << " connect failed with error: " << strerror(errno);
    Close();
    onConnectFail_(oss.str());
    return false;
  }

  return true;
}

}  // namespace net
