/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "braft/file_system_adaptor.h"
#include "braft/raft.h"
#include "brpc/server.h"
#include "rocksdb/status.h"
#include "storage/storage.h"

#include "client.h"

namespace pikiwidb {

#define RAFT_GROUPID_LEN 32

#define OK_STR "+OK"
#define DATABASES_NUM "databases_num"
#define ROCKSDB_NUM "rocksdb_num"
#define ROCKSDB_VERSION "rocksdb_version"
#define WRONG_LEADER "-ERR wrong leader"
#define RAFT_GROUP_ID "raft_group_id:"
#define NOT_LEADER "Not leader"

#define PRAFT PRaft::Instance()

// class EventLoop;
class Binlog;

enum ClusterCmdType {
  kNone,
  kJoin,
  kRemove,
};

class ClusterCmdContext {
  friend class PRaft;

 public:
  ClusterCmdContext() = default;
  ~ClusterCmdContext() = default;

  bool Set(ClusterCmdType cluster_cmd_type, PClient* client, std::string&& peer_ip, int port,
           std::string&& peer_id = "");

  void Clear();

  // @todo the function seems useless
  bool IsEmpty();

  ClusterCmdType GetClusterCmdType() { return cluster_cmd_type_; }
  PClient* GetClient() { return client_; }
  const std::string& GetPeerIp() { return peer_ip_; }
  int GetPort() { return port_; }
  const std::string& GetPeerID() { return peer_id_; }

  void ConnectTargetNode();

 private:
  ClusterCmdType cluster_cmd_type_ = ClusterCmdType::kNone;
  std::mutex mtx_;
  PClient* client_ = nullptr;
  std::string peer_ip_;
  int port_ = 0;
  std::string peer_id_;
};

class PRaftWriteDoneClosure : public braft::Closure {
 public:
  explicit PRaftWriteDoneClosure(std::promise<rocksdb::Status>&& promise) : promise_(std::move(promise)) {}

  void Run() override {
    promise_.set_value(result_);
    delete this;
  }
  void SetStatus(rocksdb::Status status) { result_ = std::move(status); }

 private:
  std::promise<rocksdb::Status> promise_;
  rocksdb::Status result_{rocksdb::Status::Aborted("Unknown error")};
};

class PRaft : public braft::StateMachine {
 public:
  PRaft() = default;
  ~PRaft() override = default;

  static PRaft& Instance();

  //===--------------------------------------------------------------------===//
  // Braft API
  //===--------------------------------------------------------------------===//
  butil::Status Init(std::string& group_id, bool initial_conf_is_null);
  butil::Status AddPeer(const std::string& peer);
  butil::Status RemovePeer(const std::string& peer);
  butil::Status DoSnapshot(int64_t self_snapshot_index = 0, bool is_sync = true);

  void ShutDown();
  void Join();
  void AppendLog(const Binlog& log, std::promise<rocksdb::Status>&& promise);
  void Clear();

  //===--------------------------------------------------------------------===//
  // Cluster command
  //===--------------------------------------------------------------------===//
  ClusterCmdContext& GetClusterCmdCtx() { return cluster_cmd_ctx_; }
  void SendNodeRequest(PClient* client);
  void SendNodeInfoRequest(PClient* client, const std::string& info_type);
  void SendNodeAddRequest(PClient* client);
  void SendNodeRemoveRequest(PClient* client);

  int ProcessClusterCmdResponse(PClient* client, const char* start, int len);
  void CheckRocksDBConfiguration(PClient* client, PClient* join_client, const std::string& reply);
  void LeaderRedirection(PClient* join_client, const std::string& reply);
  void InitializeNodeBeforeAdd(PClient* client, PClient* join_client, const std::string& reply);
  int ProcessClusterJoinCmdResponse(PClient* client, const char* start, int len);
  int ProcessClusterRemoveCmdResponse(PClient* client, const char* start, int len);

  void OnClusterCmdConnectionFailed(const std::string& err);

  bool IsLeader() const;
  void GetLeaderLeaseStatus(braft::LeaderLeaseStatus* status) const;
  std::string GetLeaderAddress() const;
  std::string GetLeaderID() const;
  std::string GetNodeID() const;
  std::string GetPeerID() const;
  std::string GetGroupID() const;
  braft::NodeStatus GetNodeStatus() const;
  butil::Status GetListPeers(std::vector<braft::PeerId>* peers);
  storage::LogIndex GetTerm(uint64_t log_index);
  storage::LogIndex GetLastLogIndex(bool is_flush = false);

  bool IsInitialized() const { return node_ != nullptr && server_ != nullptr; }

 private:
  void on_apply(braft::Iterator& iter) override;
  void on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) override;
  int on_snapshot_load(braft::SnapshotReader* reader) override;

  void on_leader_start(int64_t term) override;
  void on_leader_stop(const butil::Status& status) override;

  void on_shutdown() override;
  void on_error(const ::braft::Error& e) override;
  void on_configuration_committed(const ::braft::Configuration& conf) override;
  void on_stop_following(const ::braft::LeaderChangeContext& ctx) override;
  void on_start_following(const ::braft::LeaderChangeContext& ctx) override;

 private:
  std::unique_ptr<brpc::Server> server_{nullptr};  // brpc
  std::unique_ptr<braft::Node> node_{nullptr};
  butil::atomic<int64_t> leader_term_ = -1;
  braft::NodeOptions node_options_;  // options for raft node
  std::string raw_addr_;             // ip:port of this node

  scoped_refptr<braft::FileSystemAdaptor> snapshot_adaptor_ = nullptr;
  ClusterCmdContext cluster_cmd_ctx_;  // context for cluster join/remove command
  std::string group_id_;               // group id
  int db_id_ = 0;                      // db_id

  bool is_node_first_start_up_ = true;
};

}  // namespace pikiwidb
