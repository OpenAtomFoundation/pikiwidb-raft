/*
 * Copyright (c) 2024-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "praft.pb.h"

namespace pikiwidb {

class PRaft;
class PRaftServiceImpl : public PRaftService {
 public:
  PRaftServiceImpl() = default;
  void AddNode(::google::protobuf::RpcController* controller, const ::pikiwidb::NodeAddRequest* request,
               ::pikiwidb::NodeAddResponse* response, ::google::protobuf::Closure* done);

  void RemoveNode(::google::protobuf::RpcController* controller, const ::pikiwidb::NodeRemoveRequest* request,
                  ::pikiwidb::NodeRemoveResponse* response, ::google::protobuf::Closure* done);
};

}  // namespace pikiwidb
