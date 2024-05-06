/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "praft.pb.h"

namespace pikiwidb {

class PRaft;

class DummyServiceImpl : public DummyService {
 public:
  explicit DummyServiceImpl(PRaft* praft) : praft_(praft) {}
  void DummyMethod(::google::protobuf::RpcController* controller, const ::pikiwidb::DummyRequest* request,
                   ::pikiwidb::DummyResponse* response, ::google::protobuf::Closure* done) override {}

 private:
  PRaft* praft_ = nullptr;
};

}  // namespace pikiwidb
