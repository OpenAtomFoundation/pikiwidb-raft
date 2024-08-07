/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_admin.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "db.h"

#include "braft/raft.h"
#include "pstd_string.h"
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
  DEFER { PSTORE.GetBackend(currentDBIndex).get()->UnLock(); };

  std::string db_path = g_config.db_path.ToString() + std::to_string(currentDBIndex);
  std::string path_temp = db_path;
  path_temp.append("_deleting/");
  pstd::RenameFile(db_path, path_temp);

  auto s = PSTORE.GetBackend(currentDBIndex)->Open();
  if (!s.ok()) {
    client->SetRes(CmdRes::kErrOther, "flushdb failed");
    return;
  }
  auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
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

InfoCmd::InfoCmd(const std::string& name, int16_t arity) : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

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

SortCmd::SortCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool SortCmd::DoInitial(PClient* client) {
  InitialArgument();
  client->SetKey(client->argv_[1]);
  size_t argc = client->argv_.size();
  for (int i = 2; i < argc; ++i) {
    int leftargs = argc - i - 1;
    if (strcasecmp(client->argv_[i].data(), "asc") == 0) {
      desc_ = 0;
    } else if (strcasecmp(client->argv_[i].data(), "desc") == 0) {
      desc_ = 1;
    } else if (strcasecmp(client->argv_[i].data(), "alpha") == 0) {
      alpha_ = 1;
    } else if (strcasecmp(client->argv_[i].data(), "limit") == 0 && leftargs >= 2) {
      if (pstd::String2int(client->argv_[i + 1], &offset_) == 0 ||
          pstd::String2int(client->argv_[i + 2], &count_) == 0) {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      i += 2;
    } else if (strcasecmp(client->argv_[i].data(), "store") == 0 && leftargs >= 1) {
      store_key_ = client->argv_[i + 1];
      i++;
    } else if (strcasecmp(client->argv_[i].data(), "by") == 0 && leftargs >= 1) {
      sortby_ = client->argv_[i + 1];
      if (sortby_.find('*') == std::string::npos) {
        dontsort_ = 1;
      }
      i++;
    } else if (strcasecmp(client->argv_[i].data(), "get") == 0 && leftargs >= 1) {
      get_patterns_.push_back(client->argv_[i + 1]);
      i++;
    } else {
      client->SetRes(CmdRes::kSyntaxErr);
      return false;
    }
  }

  Status s;
  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRange(client->Key(), 0, -1, &ret_);
  if (s.ok()) {
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }

  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SMembers(client->Key(), &ret_);
  if (s.ok()) {
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }

  std::vector<storage::ScoreMember> score_members;
  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRange(client->Key(), 0, -1, &score_members);
  if (s.ok()) {
    for (auto& c : score_members) {
      ret_.emplace_back(c.member);
    }
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }
  client->SetRes(CmdRes::kErrOther, "Unknown Type");
  return false;
}

void SortCmd::DoCmd(PClient* client) {
  std::vector<RedisSortObject> sort_ret(ret_.size());
  for (size_t i = 0; i < ret_.size(); ++i) {
    sort_ret[i].obj = ret_[i];
  }

  if (!dontsort_) {
    for (size_t i = 0; i < ret_.size(); ++i) {
      std::string byval;
      if (!sortby_.empty()) {
        auto lookup = lookupKeyByPattern(client, sortby_, ret_[i]);
        if (!lookup.has_value()) {
          byval = ret_[i];
        } else {
          byval = std::move(lookup.value());
        }
      } else {
        byval = ret_[i];
      }

      if (alpha_) {
        sort_ret[i].u = byval;
      } else {
        double double_byval;
        if (pstd::String2d(byval, &double_byval)) {
          sort_ret[i].u = double_byval;
        } else {
          client->SetRes(CmdRes::kErrOther, "One or more scores can't be converted into double");
          return;
        }
      }
    }

    std::sort(sort_ret.begin(), sort_ret.end(), [this](const RedisSortObject& a, const RedisSortObject& b) {
      if (this->alpha_) {
        std::string score_a = std::get<std::string>(a.u);
        std::string score_b = std::get<std::string>(b.u);
        return !this->desc_ ? score_a < score_b : score_a > score_b;
      } else {
        double score_a = std::get<double>(a.u);
        double score_b = std::get<double>(b.u);
        return !this->desc_ ? score_a < score_b : score_a > score_b;
      }
    });

    size_t sort_size = sort_ret.size();

    count_ = count_ >= 0 ? count_ : sort_size;
    offset_ = (offset_ >= 0 && offset_ < sort_size) ? offset_ : sort_size;
    count_ = (offset_ + count_ < sort_size) ? count_ : sort_size - offset_;

    size_t m_start = offset_;
    size_t m_end = offset_ + count_;

    ret_.clear();
    if (get_patterns_.empty()) {
      get_patterns_.emplace_back("#");
    }

    for (; m_start < m_end; m_start++) {
      for (const std::string& pattern : get_patterns_) {
        std::optional<std::string> val = lookupKeyByPattern(client, pattern, sort_ret[m_start].obj);
        if (val.has_value()) {
          ret_.push_back(val.value());
        } else {
          ret_.emplace_back("");
        }
      }
    }
  }

  if (store_key_.empty()) {
    client->AppendStringVector(ret_);
  } else {
    uint64_t reply_num = 0;
    storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPush(store_key_, ret_, &reply_num);
    if (s.ok()) {
      client->AppendInteger(reply_num);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
  }
}

std::optional<std::string> SortCmd::lookupKeyByPattern(PClient* client, const std::string& pattern,
                                                       const std::string& subst) {
  if (pattern == "#") {
    return subst;
  }

  auto match_pos = pattern.find('*');
  if (match_pos == std::string::npos) {
    return std::nullopt;
  }

  std::string field;
  auto arrow_pos = pattern.find("->", match_pos + 1);
  if (arrow_pos != std::string::npos && arrow_pos + 2 < pattern.size()) {
    field = pattern.substr(arrow_pos + 2);
  }

  std::string key = pattern.substr(0, match_pos + 1);
  key.replace(match_pos, 1, subst);

  std::string value;
  storage::Status s;
  if (!field.empty()) {
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HGet(key, field, &value);
  } else {
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Get(key, &value);
  }

  if (!s.ok()) {
    return std::nullopt;
  }

  return value;
}

void SortCmd::InitialArgument() {
  desc_ = 0;
  alpha_ = 0;
  offset_ = 0;
  count_ = -1;
  dontsort_ = 0;
  store_key_.clear();
  sortby_.clear();
  get_patterns_.clear();
  ret_.clear();
}
}  // namespace pikiwidb
