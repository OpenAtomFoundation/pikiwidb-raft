/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "praft.h"

#include <cassert>

#include "braft/raft.h"
#include "braft/snapshot.h"
#include "braft/util.h"
#include "brpc/server.h"
#include "gflags/gflags.h"

#include "pstd/log.h"
#include "pstd/pstd_string.h"

#include "binlog.pb.h"
#include "config.h"
#include "pikiwidb.h"
#include "replication.h"
#include "store.h"

#include "praft_service.h"
#include "psnapshot.h"

namespace braft {
DECLARE_bool(raft_enable_leader_lease);
}  // namespace braft

#define ERROR_LOG_AND_STATUS(msg) \
  ({                              \
    ERROR(msg);                   \
    butil::Status(EINVAL, msg);   \
  })

namespace pikiwidb {

bool ClusterCmdContext::Set(ClusterCmdType cluster_cmd_type, PClient* client, std::string&& peer_ip, int port,
                            std::string&& peer_id) {
  std::unique_lock<std::mutex> lck(mtx_);
  if (client_ != nullptr) {
    return false;
  }
  assert(client);
  cluster_cmd_type_ = cluster_cmd_type;
  client_ = client;
  peer_ip_ = std::move(peer_ip);
  port_ = port;
  peer_id_ = std::move(peer_id);
  return true;
}

void ClusterCmdContext::Clear() {
  std::unique_lock<std::mutex> lck(mtx_);
  cluster_cmd_type_ = ClusterCmdType::kNone;
  client_ = nullptr;
  peer_ip_.clear();
  port_ = 0;
  peer_id_.clear();
}

bool ClusterCmdContext::IsEmpty() {
  std::unique_lock<std::mutex> lck(mtx_);
  return client_ == nullptr;
}

void ClusterCmdContext::ConnectTargetNode() {
  auto ip = PREPL.GetMasterAddr().GetIP();
  auto port = PREPL.GetMasterAddr().GetPort();
  if (ip == peer_ip_ && port == port_ && PREPL.GetMasterState() == kPReplStateConnected) {
    PRAFT.SendNodeRequest(PREPL.GetMaster());
    return;
  }

  // reconnect
  auto fail_cb = [&](const std::string& err) {
    INFO("Failed to connect to cluster node, err: {}", err);
    PRAFT.OnClusterCmdConnectionFailed(err);
  };
  PREPL.SetFailCallback(fail_cb);
  PREPL.SetMasterState(kPReplStateNone);
  PREPL.SetMasterAddr(peer_ip_.c_str(), port_);
}

PRaft& PRaft::Instance() {
  static PRaft store;
  return store;
}

butil::Status PRaft::Init(std::string& group_id, bool initial_conf_is_null) {
  if (node_ && server_) {
    return {0, "OK"};
  }

  server_ = std::make_unique<brpc::Server>();
  auto port = g_config.port + pikiwidb::g_config.raft_port_offset;
  // Add your service into RPC server
  DummyServiceImpl service(&PRAFT);
  if (server_->AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    server_.reset();
    return ERROR_LOG_AND_STATUS("Failed to add service");
  }
  // raft can share the same RPC server. Notice the second parameter, because
  // adding services into a running server is not allowed and the listen
  // address of this server is impossible to get before the server starts. You
  // have to specify the address of the server.
  if (braft::add_service(server_.get(), port) != 0) {
    server_.reset();
    return ERROR_LOG_AND_STATUS("Failed to add raft service");
  }

  // It's recommended to start the server before Counter is started to avoid
  // the case that it becomes the leader while the service is unreacheable by
  // clients.
  // Notice the default options of server is used here. Check out details from
  // the doc of brpc if you would like change some option;
  if (server_->Start(port, nullptr) != 0) {
    server_.reset();
    return ERROR_LOG_AND_STATUS("Failed to start server");
  }
  // It's ok to start PRaft;
  assert(group_id.size() == RAFT_GROUPID_LEN);
  this->group_id_ = group_id;

  // FIXME: g_config.ip is default to 127.0.0.0, which may not work in cluster.
  raw_addr_ = g_config.ip.ToString() + ":" + std::to_string(port);
  butil::ip_t ip;
  auto ret = butil::str2ip(g_config.ip.ToString().c_str(), &ip);
  if (ret != 0) {
    server_.reset();
    return ERROR_LOG_AND_STATUS("Failed to convert str_ip to butil::ip_t");
  }
  butil::EndPoint addr(ip, port);

  // Default init in one node.
  // initial_conf takes effect only when the replication group is started from an empty node.
  // The Configuration is restored from the snapshot and log files when the data in the replication group is not empty.
  //  initial_conf is used only to create replication groups.
  //  The first node adds itself to initial_conf and then calls add_peer to add other nodes.
  //  Set initial_conf to empty for other nodes.
  //  You can also start empty nodes simultaneously by setting the same inital_conf(ip:port of multiple nodes) for
  //  multiple nodes.
  std::string initial_conf;
  if (!initial_conf_is_null) {
    initial_conf = raw_addr_ + ":0,";
  }
  if (node_options_.initial_conf.parse_from(initial_conf) != 0) {
    server_.reset();
    return ERROR_LOG_AND_STATUS("Failed to parse configuration");
  }

  // node_options_.election_timeout_ms = FLAGS_election_timeout_ms;
  node_options_.fsm = this;
  node_options_.node_owns_fsm = false;
  node_options_.snapshot_interval_s = 0;
  std::string prefix = "local://" + g_config.db_path.ToString() + std::to_string(db_id_) + "/_praft";
  node_options_.log_uri = prefix + "/log";
  node_options_.raft_meta_uri = prefix + "/raft_meta";
  node_options_.snapshot_uri = prefix + "/snapshot";
  // node_options_.disable_cli = FLAGS_disable_cli;
  snapshot_adaptor_ = new PPosixFileSystemAdaptor();
  node_options_.snapshot_file_system_adaptor = &snapshot_adaptor_;

  node_ = std::make_unique<braft::Node>("pikiwidb", braft::PeerId(addr));  // group_id
  if (node_->init(node_options_) != 0) {
    server_.reset();
    node_.reset();
    return ERROR_LOG_AND_STATUS("Failed to init raft node");
  }

  // enable leader lease
  braft::FLAGS_raft_enable_leader_lease = true;

  return {0, "OK"};
}

bool PRaft::IsLeader() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return false;
  }

  braft::LeaderLeaseStatus lease_status;
  GetLeaderLeaseStatus(&lease_status);
  auto term = leader_term_.load(butil::memory_order_acquire);

  INFO("term : {}, lease_status : {}", term, lease_status.term);

  return term > 0 && term == lease_status.term;
}

void PRaft::GetLeaderLeaseStatus(braft::LeaderLeaseStatus* status) const {
  if (!node_) {
    ERROR("Node is not initialized");
    return;
  }

  node_->get_leader_lease_status(status);
}

std::string PRaft::GetLeaderID() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return "Failed to get leader id";
  }
  return node_->leader_id().to_string();
}

std::string PRaft::GetLeaderAddress() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return "Failed to get leader id";
  }
  auto id = node_->leader_id();
  // The cluster does not have a leader.
  if (id.is_empty()) {
    return std::string();
  }

  id.addr.port -= g_config.raft_port_offset;
  auto addr = butil::endpoint2str(id.addr);
  return addr.c_str();
}

std::string PRaft::GetNodeID() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return "Failed to get node id";
  }
  return node_->node_id().to_string();
}

std::string PRaft::GetPeerID() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return "Failed to get node id";
  }

  auto node_id = node_->node_id().to_string();
  auto pos = node_id.find(':');
  auto peer_id = node_id.substr(pos + 1, node_id.size());
  return peer_id;
}

std::string PRaft::GetGroupID() const {
  if (!node_) {
    ERROR("Node is not initialized");
    return "Failed to get cluster id";
  }
  return group_id_;
}

braft::NodeStatus PRaft::GetNodeStatus() const {
  braft::NodeStatus node_status;
  if (!node_) {
    ERROR("Node is not initialized");
  } else {
    node_->get_status(&node_status);
  }

  return node_status;
}

butil::Status PRaft::GetListPeers(std::vector<braft::PeerId>* peers) {
  if (!node_) {
    ERROR_LOG_AND_STATUS("Node is not initialized");
  }
  return node_->list_peers(peers);
}

storage::LogIndex PRaft::GetTerm(uint64_t log_index) {
  if (!node_) {
    ERROR("Node is not initialized");
    return 0;
  }

  return node_->get_term(log_index);
}

storage::LogIndex PRaft::GetLastLogIndex(bool is_flush) {
  if (!node_) {
    ERROR("Node is not initialized");
    return 0;
  }

  return node_->get_last_log_index(is_flush);
}

void PRaft::SendNodeRequest(PClient* client) {
  assert(client);

  auto cluster_cmd_type = cluster_cmd_ctx_.GetClusterCmdType();
  switch (cluster_cmd_type) {
    case ClusterCmdType::kJoin:
      SendNodeInfoRequest(client, "DATA");
      break;
    case ClusterCmdType::kRemove:
      SendNodeRemoveRequest(client);
      break;
    default:
      client->SetRes(CmdRes::kErrOther, "the command sent to the leader is incorrect");
      break;
  }
}

// Gets the cluster id, which is used to initialize node
void PRaft::SendNodeInfoRequest(PClient* client, const std::string& info_type) {
  assert(client);

  std::string cmd_str = "INFO " + info_type + "\r\n";
  client->SendPacket(std::move(cmd_str));
  //  client->Clear();
}

void PRaft::SendNodeAddRequest(PClient* client) {
  assert(client);

  // Node id in braft are ip:port, the node id param in RAFT.NODE ADD cmd will be ignored.
  int unused_node_id = 0;
  auto port = g_config.port + pikiwidb::g_config.raft_port_offset;
  auto raw_addr = g_config.ip.ToString() + ":" + std::to_string(port);
  UnboundedBuffer req;
  req.PushData("RAFT.NODE ADD ", 14);
  req.PushData(std::to_string(unused_node_id).c_str(), std::to_string(unused_node_id).size());
  req.PushData(" ", 1);
  req.PushData(raw_addr.data(), raw_addr.size());
  req.PushData("\r\n", 2);
  client->SendPacket(req);
  //  client->Clear();
}

void PRaft::SendNodeRemoveRequest(PClient* client) {
  assert(client);

  UnboundedBuffer req;
  req.PushData("RAFT.NODE REMOVE ", 17);
  req.PushData(cluster_cmd_ctx_.GetPeerID().c_str(), cluster_cmd_ctx_.GetPeerID().size());
  req.PushData("\r\n", 2);
  client->SendPacket(req);
  client->Clear();
}

int PRaft::ProcessClusterCmdResponse(PClient* client, const char* start, int len) {
  auto cluster_cmd_type = cluster_cmd_ctx_.GetClusterCmdType();
  int ret = 0;
  switch (cluster_cmd_type) {
    case ClusterCmdType::kJoin:
      ret = PRAFT.ProcessClusterJoinCmdResponse(client, start, len);
      break;
    case ClusterCmdType::kRemove:
      ret = PRAFT.ProcessClusterRemoveCmdResponse(client, start, len);
      break;
    default:
      client->SetRes(CmdRes::kErrOther, "RAFT.CLUSTER response supports JOIN/REMOVE only");
      break;
  }

  return ret;
}

void PRaft::CheckRocksDBConfiguration(PClient* client, PClient* join_client, const std::string& reply) {
  int databases_num = 0;
  int rocksdb_num = 0;
  std::string rockdb_version;
  std::string line;
  std::istringstream iss(reply);

  while (std::getline(iss, line)) {
    std::string::size_type pos = line.find(':');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);

      if (key == DATABASES_NUM && pstd::String2int(value, &databases_num) == 0) {
        join_client->SetRes(CmdRes::kErrOther, "Config of databases_num invalid");
        join_client->SendPacket();
        //        join_client->Clear();
        // If the join fails, clear clusterContext and set it again by using the join command
        cluster_cmd_ctx_.Clear();
      } else if (key == ROCKSDB_NUM && pstd::String2int(value, &rocksdb_num) == 0) {
        join_client->SetRes(CmdRes::kErrOther, "Config of rocksdb_num invalid");
        join_client->SendPacket();
        //        join_client->Clear();
        // If the join fails, clear clusterContext and set it again by using the join command
        cluster_cmd_ctx_.Clear();
      } else if (key == ROCKSDB_VERSION) {
        rockdb_version = pstd::StringTrimRight(value, "\r");
      }
    }
  }

  int current_databases_num = pikiwidb::g_config.databases;
  int current_rocksdb_num = pikiwidb::g_config.db_instance_num;
  std::string current_rocksdb_version = ROCKSDB_NAMESPACE::GetRocksVersionAsString();
  if (current_databases_num != databases_num || current_rocksdb_num != rocksdb_num ||
      current_rocksdb_version != rockdb_version) {
    join_client->SetRes(CmdRes::kErrOther, "Config of databases_num, rocksdb_num or rocksdb_version mismatch");
    join_client->SendPacket();
    //    join_client->Clear();
    // If the join fails, clear clusterContext and set it again by using the join command
    cluster_cmd_ctx_.Clear();
  } else {
    SendNodeInfoRequest(client, "RAFT");
  }
}

void PRaft::LeaderRedirection(PClient* join_client, const std::string& reply) {
  // Resolve the ip address of the leader
  pstd::StringTrimLeft(reply, WRONG_LEADER);
  pstd::StringTrim(reply);
  braft::PeerId peerId;
  peerId.parse(reply);
  auto peer_ip = std::string(butil::ip2str(peerId.addr.ip).c_str());
  auto port = peerId.addr.port;

  // Reset the target of the connection
  cluster_cmd_ctx_.Clear();
  auto ret = PRAFT.GetClusterCmdCtx().Set(ClusterCmdType::kJoin, join_client, std::move(peer_ip), port);
  if (!ret) {  // other clients have joined
    join_client->SetRes(CmdRes::kErrOther, "Other clients have joined");
    join_client->SendPacket();
    //    join_client->Clear();
    return;
  }
  PRAFT.GetClusterCmdCtx().ConnectTargetNode();

  // Not reply any message here, we will reply after the connection is established.
  join_client->Clear();
}

void PRaft::InitializeNodeBeforeAdd(PClient* client, PClient* join_client, const std::string& reply) {
  std::string prefix = RAFT_GROUP_ID;
  std::string::size_type prefix_length = prefix.length();
  std::string::size_type group_id_start = reply.find(prefix);
  group_id_start += prefix_length;  // locate the start location of "raft_group_id"
  std::string::size_type group_id_end = reply.find("\r\n", group_id_start);
  if (group_id_end != std::string::npos) {
    std::string raft_group_id = reply.substr(group_id_start, group_id_end - group_id_start);
    // initialize the slave node
    auto s = PRAFT.Init(raft_group_id, true);
    if (!s.ok()) {
      join_client->SetRes(CmdRes::kErrOther, s.error_str());
      join_client->SendPacket();
      //      join_client->Clear();
      // If the join fails, clear clusterContext and set it again by using the join command
      cluster_cmd_ctx_.Clear();
      return;
    }

    PRAFT.SendNodeAddRequest(client);
  } else {
    ERROR("Joined Raft cluster fail, because of invalid raft_group_id");
    join_client->SetRes(CmdRes::kErrOther, "Invalid raft_group_id");
    join_client->SendPacket();
    //    join_client->Clear();
    // If the join fails, clear clusterContext and set it again by using the join command
    cluster_cmd_ctx_.Clear();
  }
}

int PRaft::ProcessClusterJoinCmdResponse(PClient* client, const char* start, int len) {
  assert(start);
  auto join_client = cluster_cmd_ctx_.GetClient();
  if (!join_client) {
    WARN("No client when processing cluster join cmd response.");
    return 0;
  }

  std::string reply(start, len);
  if (reply.find(OK_STR) != std::string::npos) {
    INFO("Joined Raft cluster, node id: {}, group_id: {}", PRAFT.GetNodeID(), PRAFT.group_id_);
    join_client->SetRes(CmdRes::kOK);
    join_client->SendPacket();
    //    join_client->Clear();
    // If the join fails, clear clusterContext and set it again by using the join command
    cluster_cmd_ctx_.Clear();
  } else if (reply.find(DATABASES_NUM) != std::string::npos) {
    CheckRocksDBConfiguration(client, join_client, reply);
  } else if (reply.find(WRONG_LEADER) != std::string::npos) {
    LeaderRedirection(join_client, reply);
  } else if (reply.find(RAFT_GROUP_ID) != std::string::npos) {
    InitializeNodeBeforeAdd(client, join_client, reply);
  } else {
    ERROR("Joined Raft cluster fail, str: {}", reply);
    join_client->SetRes(CmdRes::kErrOther, reply);
    join_client->SendPacket();
    //    join_client->Clear();
    // If the join fails, clear clusterContext and set it again by using the join command
    cluster_cmd_ctx_.Clear();
  }

  return len;
}

int PRaft::ProcessClusterRemoveCmdResponse(PClient* client, const char* start, int len) {
  assert(start);
  auto remove_client = cluster_cmd_ctx_.GetClient();
  if (!remove_client) {
    WARN("No client when processing cluster remove cmd response.");
    return 0;
  }

  std::string reply(start, len);
  if (reply.find(OK_STR) != std::string::npos) {
    INFO("Removed Raft cluster, node id: {}, group_id: {}", PRAFT.GetNodeID(), PRAFT.group_id_);
    ShutDown();
    Join();
    Clear();

    remove_client->SetRes(CmdRes::kOK);
    remove_client->SendPacket();
    //    remove_client->Clear();
  } else if (reply.find(NOT_LEADER) != std::string::npos) {
    auto remove_client = cluster_cmd_ctx_.GetClient();
    remove_client->Clear();
    remove_client->Reexecutecommand();
  } else {
    ERROR("Removed Raft cluster fail, str: {}", reply);
    remove_client->SetRes(CmdRes::kErrOther, reply);
    remove_client->SendPacket();
    //    remove_client->Clear();
  }

  // If the remove fails, clear clusterContext and set it again by using the join command
  cluster_cmd_ctx_.Clear();

  return len;
}

butil::Status PRaft::AddPeer(const std::string& peer) {
  if (!node_) {
    ERROR_LOG_AND_STATUS("Node is not initialized");
  }

  braft::SynchronizedClosure done;
  node_->add_peer(peer, &done);
  done.wait();

  if (!done.status().ok()) {
    WARN("Failed to add peer {} to node {}, status: {}", peer, node_->node_id().to_string(), done.status().error_str());
    return done.status();
  }

  return {0, "OK"};
}

butil::Status PRaft::RemovePeer(const std::string& peer) {
  if (!node_) {
    return ERROR_LOG_AND_STATUS("Node is not initialized");
  }

  braft::SynchronizedClosure done;
  node_->remove_peer(peer, &done);
  done.wait();

  if (!done.status().ok()) {
    WARN("Failed to remove peer {} from node {}, status: {}", peer, node_->node_id().to_string(),
         done.status().error_str());
    return done.status();
  }

  return {0, "OK"};
}

butil::Status PRaft::DoSnapshot(int64_t self_snapshot_index, bool is_sync) {
  if (!node_) {
    return ERROR_LOG_AND_STATUS("Node is not initialized");
  }

  if (is_sync) {
    braft::SynchronizedClosure done;
    node_->snapshot(&done, self_snapshot_index);
    done.wait();
    return done.status();
  } else {
    node_->snapshot(nullptr, self_snapshot_index);
    return butil::Status{};
  }
}

void PRaft::OnClusterCmdConnectionFailed(const std::string& err) {
  auto cli = cluster_cmd_ctx_.GetClient();
  if (cli) {
    cli->SetRes(CmdRes::kErrOther, "Failed to connect to cluster for join or remove, please check logs " + err);
    cli->SendPacket();
    //    cli->Clear();
  }
  cluster_cmd_ctx_.Clear();

  PREPL.GetMasterAddr().Clear();
}

// Shut this node and server down.
void PRaft::ShutDown() {
  if (node_) {
    node_->shutdown(nullptr);
  }

  if (server_) {
    server_->Stop(0);
  }
}

// Blocking this thread until the node is eventually down.
void PRaft::Join() {
  if (node_) {
    node_->join();
  }

  if (server_) {
    server_->Join();
  }
}

void PRaft::AppendLog(const Binlog& log, std::promise<rocksdb::Status>&& promise) {
  assert(node_);
  assert(node_->is_leader());
  butil::IOBuf data;
  butil::IOBufAsZeroCopyOutputStream wrapper(&data);
  auto done = new PRaftWriteDoneClosure(std::move(promise));
  if (!log.SerializeToZeroCopyStream(&wrapper)) {
    done->SetStatus(rocksdb::Status::Incomplete("Failed to serialize binlog"));
    done->Run();
    return;
  }
  DEBUG("append binlog: {}", log.ShortDebugString());
  braft::Task task;
  task.data = &data;
  task.done = done;
  node_->apply(task);
}

// @braft::StateMachine
void PRaft::Clear() {
  if (node_) {
    node_.reset();
  }

  if (server_) {
    server_.reset();
  }
}

void PRaft::on_apply(braft::Iterator& iter) {
  // A batch of tasks are committed, which must be processed through
  for (; iter.valid(); iter.next()) {
    auto done = iter.done();
    brpc::ClosureGuard done_guard(done);

    Binlog log;
    butil::IOBufAsZeroCopyInputStream wrapper(iter.data());
    bool success = log.ParseFromZeroCopyStream(&wrapper);
    DEBUG("apply binlog{}: {}", iter.index(), log.ShortDebugString());

    if (!success) {
      static constexpr std::string_view kMsg = "Failed to parse from protobuf when on_apply";
      ERROR(kMsg);
      if (done) {  // in leader
        dynamic_cast<PRaftWriteDoneClosure*>(done)->SetStatus(rocksdb::Status::Incomplete(kMsg));
      }
      braft::run_closure_in_bthread(done_guard.release());
      return;
    }

    auto s = PSTORE.GetBackend(log.db_id())->GetStorage()->OnBinlogWrite(log, iter.index());
    if (done) {  // in leader
      dynamic_cast<PRaftWriteDoneClosure*>(done)->SetStatus(s);
    }
    //  _applied_index = iter.index(); // consider to maintain a member applied_idx
    braft::run_closure_in_bthread(done_guard.release());
  }
}

void PRaft::on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) {
  assert(writer);
  brpc::ClosureGuard done_guard(done);
}

int PRaft::on_snapshot_load(braft::SnapshotReader* reader) {
  CHECK(!IsLeader()) << "Leader is not supposed to load snapshot";
  assert(reader);

  if (is_node_first_start_up_) {
    // get replay point of one db's
    /*
    1. When a node starts normally, because all memory data is flushed to disks and
       snapshots are truncated to the latest, the flush-index and apply-index are the
       same when the node starts, so the maximum log index should be obtained.
    2. When a node is improperly shut down and restarted, the minimum flush-index should
       be obtained as the starting point for fault recovery.
    */
    uint64_t replay_point = PSTORE.GetBackend(db_id_)->GetStorage()->GetSmallestFlushedLogIndex();
    node_->set_self_playback_point(replay_point);
    is_node_first_start_up_ = false;
    INFO("set replay_point: {}", replay_point);

    /*
    If a node has just joined the cluster and does not have any data,
    it does not load the local snapshot at startup. Therefore,
    LoadDBFromCheckPoint is required after loading the snapshot from the leader.
    */
    if (GetLastLogIndex() != 0) {
      return 0;
    }
  }

  // 3. When a snapshot is installed on a node, you do not need to set a playback point.
  auto reader_path = reader->get_path();                             // xx/snapshot_0000001
  auto path = g_config.db_path.ToString() + std::to_string(db_id_);  // db/db_id
  TasksVector tasks(1, {TaskType::kLoadDBFromCheckpoint, db_id_, {{TaskArg::kCheckpointPath, reader_path}}, true});
  PSTORE.HandleTaskSpecificDB(tasks);
  INFO("load snapshot success!");
  return 0;
}

void PRaft::on_leader_start(int64_t term) {
  leader_term_.store(term, butil::memory_order_release);
  LOG(INFO) << "Node becomes leader, term : " << term;
}

void PRaft::on_leader_stop(const butil::Status& status) {
  leader_term_.store(-1, butil::memory_order_release);
  LOG(INFO) << "Node stepped down : " << status;
}

void PRaft::on_shutdown() { LOG(INFO) << "This node is down"; }

void PRaft::on_error(const ::braft::Error& e) { LOG(ERROR) << "Met raft error " << e; }

void PRaft::on_configuration_committed(const ::braft::Configuration& conf) {
  LOG(INFO) << "Configuration of this group is " << conf;
}

void PRaft::on_stop_following(const ::braft::LeaderChangeContext& ctx) { LOG(INFO) << "Node stops following " << ctx; }

void PRaft::on_start_following(const ::braft::LeaderChangeContext& ctx) { LOG(INFO) << "Node start following " << ctx; }

}  // namespace pikiwidb
