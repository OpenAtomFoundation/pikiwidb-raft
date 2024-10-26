/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "pd.pb.h"

namespace pikiwidb {

class PlacementDriverServiceImpl : public PlacementDriverService {
 public:
  PlacementDriverServiceImpl() = default;

  void CreateAllRegions(::google::protobuf::RpcController* controller,
                        const ::pikiwidb::CreateAllRegionsRequest* request,
                        ::pikiwidb::CreateAllRegionsResponse* response, ::google::protobuf::Closure* done) override;

  void DeleteAllRegions(::google::protobuf::RpcController* controller,
                        const ::pikiwidb::DeleteAllRegionsRequest* request,
                        ::pikiwidb::DeleteAllRegionsResponse* response, ::google::protobuf::Closure* done) override;

  void AddStore(::google::protobuf::RpcController* controller, const ::pikiwidb::AddStoreRequest* request,
                ::pikiwidb::AddStoreResponse* response, ::google::protobuf::Closure* done) override;

  void RemoveStore(::google::protobuf::RpcController* controller, const ::pikiwidb::RemoveStoreRequest* request,
                   ::pikiwidb::RemoveStoreResponse* response, ::google::protobuf::Closure* done) override;

  void GetClusterInfo(::google::protobuf::RpcController* controller, const ::pikiwidb::GetClusterInfoRequest* request,
                      ::pikiwidb::GetClusterInfoResponse* response, ::google::protobuf::Closure* done) override;

  void OpenPDScheduling(::google::protobuf::RpcController* controller,
                        const ::pikiwidb::OpenPDSchedulingRequest* request,
                        ::pikiwidb::OpenPDSchedulingResponse* response, ::google::protobuf::Closure* done) override;

  void ClosePDScheduling(::google::protobuf::RpcController* controller,
                         const ::pikiwidb::ClosePDSchedulingRequest* request,
                         ::pikiwidb::ClosePDSchedulingResponse* response, ::google::protobuf::Closure* done) override;
};

}  // namespace pikiwidb