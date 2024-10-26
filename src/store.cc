// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Implemented a set of functions and instructions for
  coordinating external commands and interfacing with RocksDB.
 */

#include "store.h"

#include "config.h"
#include "db.h"
#include "praft/praft_service.h"
#include "pstd/log.h"
#include "pstd/pstd_string.h"

namespace pikiwidb {
PStore::~PStore() { INFO("STORE is closing..."); }

PStore& PStore::Instance() {
  static PStore store;
  return store;
}

void PStore::Init(int db_number) {
  db_number_ = db_number;
  backends_.reserve(db_number_);
  for (int i = 0; i < db_number_; i++) {
    auto db = std::make_unique<DB>(i, g_config.db_path);
    db->Open();
    backends_.push_back(std::move(db));
    INFO("Open DB_{} success!", i);
  }

  auto rpc_ip = g_config.ip.ToString();
  auto rpc_port =
      g_config.port.load(std::memory_order_relaxed) + g_config.raft_port_offset.load(std::memory_order_relaxed);

  if (0 != butil::str2endpoint(fmt::format("{}:{}", rpc_ip, std::to_string(rpc_port)).c_str(), &endpoint_)) {
    return ERROR("Wrong endpoint format");
  }

  if (0 != braft::add_service(GetRpcServer(), endpoint_)) {
    return ERROR("Failed to add raft service to rpc server");
  }

  if (0 != rpc_server_->AddService(dynamic_cast<google::protobuf::Service*>(praft_service_.get()),
                                   brpc::SERVER_OWNS_SERVICE)) {
    return ERROR("Failed to add praft service to rpc server");
  }

  if (0 != rpc_server_->Start(endpoint_, nullptr)) {
    return ERROR("Failed to start rpc server");
  }
  INFO("Started RPC server successfully on addr {}", butil::endpoint2str(endpoint_).c_str());
  INFO("PSTORE Init success!");
}

void PStore::HandleTaskSpecificDB(const TasksVector& tasks) {
  std::for_each(tasks.begin(), tasks.end(), [this](const auto& task) {
    if (task.db < 0 || task.db >= db_number_) {
      WARN("The database index is out of range.");
      return;
    }
    auto& db = backends_.at(task.db);
    switch (task.type) {
      case kCheckpoint: {
        if (auto s = task.args.find(kCheckpointPath); s == task.args.end()) {
          WARN("The critical parameter 'path' is missing for do a checkpoint.");
          return;
        }
        auto path = task.args.find(kCheckpointPath)->second;
        pstd::TrimSlash(path);
        db->CreateCheckpoint(path, task.sync);
        break;
      }
      case kLoadDBFromCheckpoint: {
        if (auto s = task.args.find(kCheckpointPath); s == task.args.end()) {
          WARN("The critical parameter 'path' is missing for load a checkpoint.");
          return;
        }
        auto path = task.args.find(kCheckpointPath)->second;
        pstd::TrimSlash(path);
        db->LoadDBFromCheckpoint(path, task.sync);
        break;
      }
      case kEmpty: {
        WARN("A empty task was passed in, not doing anything.");
        break;
      }
      default:
        break;
    }
  });
}

bool PStore::AddRegion(const std::string& group_id, uint32_t dbno) {
  std::lock_guard<std::shared_mutex> lock(rw_mutex_);
  if (region_map_.find(group_id) != region_map_.end()) {
    return false;
  }
  region_map_.emplace(group_id, dbno);
  return true;
}

bool PStore::RemoveRegion(const std::string& group_id) {
  std::lock_guard<std::shared_mutex> lock(rw_mutex_);
  if (region_map_.find(group_id) != region_map_.end()) {
    region_map_.erase(group_id);
    return true;
  }
  return false;
}

DB* PStore::GetDBByGroupID(const std::string& group_id) const {
  std::shared_lock<std::shared_mutex> lock(rw_mutex_);
  auto it = region_map_.find(group_id);
  if (it == region_map_.end()) {
    return nullptr;
  }
  return backends_[it->second].get();
}

}  // namespace pikiwidb
