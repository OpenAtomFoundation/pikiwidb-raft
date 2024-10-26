/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "proxy.pb.h"

class ProxyServiceImpl : public ProxyService {
 public:
  void RunCommand(::google::protobuf::RpcController* cntl, const pikiwidb::proxy::RunCommandRequest* request,
                  pikiwidb::proxy::RunCommandResponse* response, ::google::protobuf::Closure* done) override;
  void GetRouteInfo(::google::protobuf::RpcController* cntl, const pikiwidb::proxy::GetRouteInfoRequest* request,
                    pikiwidb::proxy::GetRouteInfoResponse* response, ::google::protobuf::Closure* done) override;

 private:
  std::string ExecuteCommand(const std::string& command);
};
