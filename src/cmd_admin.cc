/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_admin.h"
#include "db.h"

#include "braft/raft.h"
#include "rocksdb/version.h"

#include "pikiwidb.h"
#include "praft/praft.h"
#include "pstd/env.h"

#include "store.h"

namespace pikiwidb {

CmdConfig::CmdConfig(const std::string& name, int arity) : BaseCmdGroup(name, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfig::HasSubCommand() const { return true; }

CmdConfigGet::CmdConfigGet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdConfigGet::DoInitial(PClient* client) { return true; }

void CmdConfigGet::DoCmd(PClient* client) {
  std::vector<std::string> results;
  for (int i = 0; i < client->argv_.size() - 2; i++) {
    g_config.Get(client->argv_[i + 2], &results);
  }
  client->AppendStringVector(results);
}

CmdConfigSet::CmdConfigSet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfigSet::DoInitial(PClient* client) { return true; }

void CmdConfigSet::DoCmd(PClient* client) {
  auto s = g_config.Set(client->argv_[2], client->argv_[3]);
  if (!s.ok()) {
    client->SetRes(CmdRes::kInvalidParameter);
  } else {
    client->SetRes(CmdRes::kOK);
  }
}

FlushdbCmd::FlushdbCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsExclusive | kCmdFlagsAdmin | kCmdFlagsWrite,
              kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushdbCmd::DoInitial(PClient* client) { return true; }

void FlushdbCmd::DoCmd(PClient* client) {
  int currentDBIndex = client->GetCurrentDB();
  PSTORE.GetBackend(currentDBIndex).get()->Lock();

  std::string db_path = g_config.db_path.ToString() + std::to_string(currentDBIndex);
  std::string path_temp = db_path;
  path_temp.append("_deleting/");
  pstd::RenameFile(db_path, path_temp);

  auto s = PSTORE.GetBackend(currentDBIndex)->Open();
  assert(s.ok());
  auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
  PSTORE.GetBackend(currentDBIndex).get()->UnLock();
  client->SetRes(CmdRes::kOK);
}

FlushallCmd::FlushallCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsExclusive | kCmdFlagsAdmin | kCmdFlagsWrite,
              kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushallCmd::DoInitial(PClient* client) { return true; }

void FlushallCmd::DoCmd(PClient* client) {
  for (size_t i = 0; i < g_config.databases; ++i) {
    PSTORE.GetBackend(i).get()->Lock();
    std::string db_path = g_config.db_path.ToString() + std::to_string(i);
    std::string path_temp = db_path;
    path_temp.append("_deleting/");
    pstd::RenameFile(db_path, path_temp);

    auto s = PSTORE.GetBackend(i)->Open();
    assert(s.ok());
    auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
    PSTORE.GetBackend(i).get()->UnLock();
  }
  client->SetRes(CmdRes::kOK);
}

SelectCmd::SelectCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsReadonly, kAclCategoryAdmin) {}

bool SelectCmd::DoInitial(PClient* client) { return true; }

void SelectCmd::DoCmd(PClient* client) {
  int index = atoi(client->argv_[1].c_str());
  if (index < 0 || index >= g_config.databases) {
    client->SetRes(CmdRes::kInvalidIndex, kCmdNameSelect + " DB index is out of range");
    return;
  }
  client->SetCurrentDB(index);
  client->SetRes(CmdRes::kOK);
}

ShutdownCmd::ShutdownCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin | kAclCategoryWrite) {}

bool ShutdownCmd::DoInitial(PClient* client) {
  // For now, only shutdown need check local
  if (client->PeerIP().find("127.0.0.1") == std::string::npos &&
      client->PeerIP().find(g_config.ip.ToString()) == std::string::npos) {
    client->SetRes(CmdRes::kErrOther, kCmdNameShutdown + " should be localhost");
    return false;
  }
  return true;
}

void ShutdownCmd::DoCmd(PClient* client) {
  PSTORE.GetBackend(client->GetCurrentDB())->UnLockShared();
  g_pikiwidb->Stop();
  PSTORE.GetBackend(client->GetCurrentDB())->LockShared();
  client->SetRes(CmdRes::kNone);
}

PingCmd::PingCmd(const std::string& name, int16_t arity) : BaseCmd(name, arity, kCmdFlagsFast, kAclCategoryFast) {}

bool PingCmd::DoInitial(PClient* client) { return true; }

void PingCmd::DoCmd(PClient* client) { client->SetRes(CmdRes::kPong, "PONG"); }

InfoCmd::InfoCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsReadonly, kAclCategoryAdmin) {}

bool InfoCmd::DoInitial(PClient* client) { return true; }

// @todo The info raft command is only supported for the time being
void InfoCmd::DoCmd(PClient* client) {
  if (client->argv_.size() <= 1) {
    return client->SetRes(CmdRes::kWrongNum, client->CmdName());
  }

  auto cmd = client->argv_[1];
  if (!strcasecmp(cmd.c_str(), "RAFT")) {
    InfoRaft(client);
  } else if (!strcasecmp(cmd.c_str(), "data")) {
    InfoData(client);
  } else {
    client->SetRes(CmdRes::kErrOther, "the cmd is not supported");
  }
}

/*
* INFO raft
* Querying Node Information.
* Reply:
*   raft_node_id:595100767
    raft_state:up
    raft_role:follower
    raft_is_voting:yes
    raft_leader_id:1733428433
    raft_current_term:1
    raft_num_nodes:2
    raft_num_voting_nodes:2
    raft_node1:id=1733428433,state=connected,voting=yes,addr=localhost,port=5001,last_conn_secs=5,conn_errors=0,conn_oks=1
*/
void InfoCmd::InfoRaft(PClient* client) {
  if (client->argv_.size() != 2) {
    return client->SetRes(CmdRes::kWrongNum, client->CmdName());
  }

  if (!PRAFT.IsInitialized()) {
    return client->SetRes(CmdRes::kErrOther, "Don't already cluster member");
  }

  auto node_status = PRAFT.GetNodeStatus();
  if (node_status.state == braft::State::STATE_END) {
    return client->SetRes(CmdRes::kErrOther, "Node is not initialized");
  }

  std::string message;
  message += "raft_group_id:" + PRAFT.GetGroupID() + "\r\n";
  message += "raft_node_id:" + PRAFT.GetNodeID() + "\r\n";
  message += "raft_peer_id:" + PRAFT.GetPeerID() + "\r\n";
  if (braft::is_active_state(node_status.state)) {
    message += "raft_state:up\r\n";
  } else {
    message += "raft_state:down\r\n";
  }
  message += "raft_role:" + std::string(braft::state2str(node_status.state)) + "\r\n";
  message += "raft_leader_id:" + node_status.leader_id.to_string() + "\r\n";
  message += "raft_current_term:" + std::to_string(node_status.term) + "\r\n";

  if (PRAFT.IsLeader()) {
    std::vector<braft::PeerId> peers;
    auto status = PRAFT.GetListPeers(&peers);
    if (!status.ok()) {
      return client->SetRes(CmdRes::kErrOther, status.error_str());
    }

    for (int i = 0; i < peers.size(); i++) {
      message += "raft_node" + std::to_string(i) + ":addr=" + butil::ip2str(peers[i].addr.ip).c_str() +
                 ",port=" + std::to_string(peers[i].addr.port) + "\r\n";
    }
  }

  client->AppendString(message);
}

void InfoCmd::InfoData(PClient* client) {
  if (client->argv_.size() != 2) {
    return client->SetRes(CmdRes::kWrongNum, client->CmdName());
  }

  std::string message;
  message += DATABASES_NUM + std::string(":") + std::to_string(pikiwidb::g_config.databases) + "\r\n";
  message += ROCKSDB_NUM + std::string(":") + std::to_string(pikiwidb::g_config.db_instance_num) + "\r\n";
  message += ROCKSDB_VERSION + std::string(":") + ROCKSDB_NAMESPACE::GetRocksVersionAsString() + "\r\n";

  client->AppendString(message);
}

CmdDebug::CmdDebug(const std::string& name, int arity) : BaseCmdGroup(name, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdDebug::HasSubCommand() const { return true; }

CmdDebugHelp::CmdDebugHelp(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugHelp::DoInitial(PClient* client) { return true; }

void CmdDebugHelp::DoCmd(PClient* client) { client->AppendStringVector(debugHelps); }

CmdDebugOOM::CmdDebugOOM(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugOOM::DoInitial(PClient* client) { return true; }

void CmdDebugOOM::DoCmd(PClient* client) {
  auto ptr = ::operator new(std::numeric_limits<unsigned long>::max());
  ::operator delete(ptr);
  client->SetRes(CmdRes::kErrOther);
}

CmdDebugSegfault::CmdDebugSegfault(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugSegfault::DoInitial(PClient* client) { return true; }

void CmdDebugSegfault::DoCmd(PClient* client) {
  auto ptr = reinterpret_cast<int*>(0);
  *ptr = 0;
}

}  // namespace pikiwidb
