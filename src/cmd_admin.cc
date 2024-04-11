/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fmt/format.h"

#include "cmd_admin.h"
#include "store.h"
#include "braft/raft.h"
#include "praft.h"
#include "rocksdb/version.h"

namespace pikiwidb {

CmdConfig::CmdConfig(const std::string& name, int arity) : BaseCmdGroup(name, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfig::HasSubCommand() const { return true; }

CmdConfigGet::CmdConfigGet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdConfigGet::DoInitial(PClient* client) { return true; }

void CmdConfigGet::DoCmd(PClient* client) { client->AppendString("config cmd in development"); }

CmdConfigSet::CmdConfigSet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfigSet::DoInitial(PClient* client) { return true; }

void CmdConfigSet::DoCmd(PClient* client) { client->AppendString("config cmd in development"); }

FlushdbCmd::FlushdbCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushdbCmd::DoInitial(PClient* client) { return true; }

void FlushdbCmd::DoCmd(PClient* client) {
  //  PSTORE.dirty_ += PSTORE.DBSize();
  //  PSTORE.ClearCurrentDB();
  //  Propagate(PSTORE.GetDB(), std::vector<PString>{"flushdb"});
  client->AppendString("flushdb cmd in development");
}

FlushallCmd::FlushallCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushallCmd::DoInitial(PClient* client) { return true; }

void FlushallCmd::DoCmd(PClient* client) {
  //  int currentDB = PSTORE.GetDB();
  //  std::vector<PString> param{"flushall"};
  //  DEFER {
  //    PSTORE.SelectDB(currentDB);
  //    Propagate(-1, param);
  //    PSTORE.ResetDB();
  //  };
  //
  //  for (int dbno = 0; true; ++dbno) {
  //    if (PSTORE.SelectDB(dbno) == -1) {
  //      break;
  //    }
  //    PSTORE.dirty_ += PSTORE.DBSize();
  //  }
  client->AppendString("flushall' cmd in development");
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

  std::string message("");
  message = fmt::format("{}raft_group_id:{}\r\n", message, PRAFT.GetGroupID());
  message = fmt::format("{}raft_node_id:{}\r\n", message, PRAFT.GetNodeID());
  message = fmt::format("{}raft_peer_id:{}\r\n", message, PRAFT.GetPeerID());
  if (braft::is_active_state(node_status.state)) {
    message = fmt::format("{}raft_state:up\r\n", message);
  } else {
    message = fmt::format("{}raft_state:down\r\n", message);
  }
  message = fmt::format("{}raft_role:{}\r\n", message, std::string(braft::state2str(node_status.state)));
  message = fmt::format("{}raft_leader_id:{}\r\n", message, node_status.leader_id.to_string());
  message = fmt::format("{}raft_current_term:{}\r\n", message, std::to_string(node_status.term));

  if (PRAFT.IsLeader()) {
    std::vector<braft::PeerId> peers;
    auto status = PRAFT.GetListPeers(&peers);
    if (!status.ok()) {
      return client->SetRes(CmdRes::kErrOther, status.error_str());
    }
    
    for (int i = 0; i < peers.size(); i++) {
      message = fmt::format("{}raft_node{}:addr={},port={}\r\n", message, std::to_string(i), butil::ip2str(peers[i].addr.ip).c_str(), std::to_string(peers[i].addr.port));
    }
  }

  client->AppendString(message);
}

void InfoCmd::InfoData(PClient* client) {
  if (client->argv_.size() != 2) {
    return client->SetRes(CmdRes::kWrongNum, client->CmdName());
  }

  std::string message("");
  message = fmt::format("{}databases_num:{}\r\n", message, std::to_string(pikiwidb::g_config.databases));
  message = fmt::format("{}rocksdb_num:{}\r\n", message, std::to_string(pikiwidb::g_config.db_instance_num));
  message = fmt::format("{}rockdb_version:{}\r\n", message, ROCKSDB_NAMESPACE::GetRocksVersionAsString());

  client->AppendString(message);
}

}  // namespace pikiwidb