/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "praft_service.h"

#include "fmt/format.h"
#include "store.h"

namespace pikiwidb {
void PRaftServiceImpl::AddNode(::google::protobuf::RpcController* controller, const ::pikiwidb::NodeAddRequest* request,
                               ::pikiwidb::NodeAddResponse* response, ::google::protobuf::Closure* done) {
  brpc::ClosureGuard done_guard(done);
  auto groupid = request->group_id();
  auto end_point = request->endpoint();
  auto index = request->index();
  auto role = request->role();

  auto db_ptr = PSTORE.GetDBByGroupID(groupid);
  if (!db_ptr) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorDisMatch));
    response->set_leader_endpoint(end_point);
    return;
  }
  auto praft_ptr = db_ptr->GetPRaft();
  if (!praft_ptr) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorDisMatch));
    response->set_leader_endpoint(end_point);
    return;
  }

  if (!praft_ptr->IsLeader()) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorReDirect));
    response->set_leader_endpoint(praft_ptr->GetLeaderAddress());
    return;
  }

  auto status = praft_ptr->AddPeer(end_point, index);
  if (!status.ok()) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorAddNode));
    response->set_leader_endpoint(praft_ptr->GetLeaderAddress());
    return;
  }
  response->set_success(true);
}

void PRaftServiceImpl::RemoveNode(::google::protobuf::RpcController* controller,
                                  const ::pikiwidb::NodeRemoveRequest* request,
                                  ::pikiwidb::NodeRemoveResponse* response, ::google::protobuf::Closure* done) {
  brpc::ClosureGuard done_guard(done);
  auto groupid = request->group_id();
  auto end_point = request->endpoint();
  auto index = request->index();
  auto role = request->role();

  auto db_ptr = PSTORE.GetDBByGroupID(groupid);
  if (!db_ptr) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorDisMatch));
    response->set_leader_endpoint(end_point);
    return;
  }
  auto praft_ptr = db_ptr->GetPRaft();
  if (!praft_ptr) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorDisMatch));
    response->set_leader_endpoint(end_point);
    return;
  }

  if (!praft_ptr->IsLeader()) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorReDirect));
    response->set_leader_endpoint(praft_ptr->GetLeaderAddress());
    return;
  }

  auto status = praft_ptr->RemovePeer(end_point, index);
  if (!status.ok()) {
    response->set_success(false);
    response->set_error_code(static_cast<uint32_t>(PRaftErrorCode::kErrorAddNode));
    response->set_leader_endpoint(praft_ptr->GetLeaderAddress());
    return;
  }
  response->set_success(true);
}

}  // namespace pikiwidb