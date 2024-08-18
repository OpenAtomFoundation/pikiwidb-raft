/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "base_cmd.h"

#include "fmt/core.h"

#include "praft/praft.h"

#include "common.h"
#include "config.h"
#include "log.h"
#include "pikiwidb.h"
#include "praft/praft.h"

namespace pikiwidb {

BaseCmd::BaseCmd(std::string name, int16_t arity, uint32_t flag, uint32_t aclCategory) {
  name_ = std::move(name);
  arity_ = arity;
  flag_ = flag;
  acl_category_ = aclCategory;
  cmd_id_ = g_pikiwidb->GetCmdID();
}

bool BaseCmd::CheckArg(size_t num) const {
  if (arity_ > 0) {
    return num == arity_;
  }
  return num >= -arity_;
}

std::vector<std::string> BaseCmd::CurrentKey(PClient* client) const { return std::vector<std::string>{client->Key()}; }

void BaseCmd::Execute(PClient* client) {
  DEBUG("execute command: {}", client->CmdName());

  // read consistency (lease read) / write redirection
  if (g_config.use_raft.load(std::memory_order_relaxed) && (HasFlag(kCmdFlagsReadonly) || HasFlag(kCmdFlagsWrite))) {
    if (!PRAFT.IsInitialized()) {
      return client->SetRes(CmdRes::kErrOther, "PRAFT is not initialized");
    }

    if (!PRAFT.IsLeader()) {
      auto leader_addr = PRAFT.GetLeaderAddress();
      if (leader_addr.empty()) {
        return client->SetRes(CmdRes::kErrOther, std::string("-CLUSTERDOWN No Raft leader"));
      }

      return client->SetRes(CmdRes::kErrOther, fmt::format("-MOVED {}", leader_addr));
    }
  }

  auto dbIndex = client->GetCurrentDB();
  if (!HasFlag(kCmdFlagsExclusive)) {
    PSTORE.GetBackend(dbIndex)->LockShared();
  }
  DEFER {
    if (!HasFlag(kCmdFlagsExclusive)) {
      PSTORE.GetBackend(dbIndex)->UnLockShared();
    }
  };

  if (!DoInitial(client)) {
    return;
  }
  DoCmd(client);
}

std::string BaseCmd::ToBinlog(uint32_t exec_time, uint32_t term_id, uint64_t logic_id, uint32_t filenum,
                              uint64_t offset) {
  return "";
}

void BaseCmd::DoBinlog() {}
bool BaseCmd::HasFlag(uint32_t flag) const { return flag_ & flag; }
void BaseCmd::SetFlag(uint32_t flag) { flag_ |= flag; }
void BaseCmd::ResetFlag(uint32_t flag) { flag_ &= ~flag; }
bool BaseCmd::HasSubCommand() const { return false; }
BaseCmd* BaseCmd::GetSubCmd(const std::string& cmdName) { return nullptr; }
uint32_t BaseCmd::AclCategory() const { return acl_category_; }
void BaseCmd::AddAclCategory(uint32_t aclCategory) { acl_category_ |= aclCategory; }
std::string BaseCmd::Name() const { return name_; }
// CmdRes& BaseCommand::Res() { return res_; }
// void BaseCommand::SetResp(const std::shared_ptr<std::string>& resp) { resp_ = resp; }
// std::shared_ptr<std::string> BaseCommand::GetResp() { return resp_.lock(); }
uint32_t BaseCmd::GetCmdID() const { return cmd_id_; }

// BaseCmdGroup
BaseCmdGroup::BaseCmdGroup(const std::string& name, uint32_t flag) : BaseCmdGroup(name, -2, flag) {}
BaseCmdGroup::BaseCmdGroup(const std::string& name, int16_t arity, uint32_t flag) : BaseCmd(name, arity, flag, 0) {}

void BaseCmdGroup::AddSubCmd(std::unique_ptr<BaseCmd> cmd) { subCmds_[cmd->Name()] = std::move(cmd); }

BaseCmd* BaseCmdGroup::GetSubCmd(const std::string& cmdName) {
  auto subCmd = subCmds_.find(cmdName);
  if (subCmd == subCmds_.end()) {
    return nullptr;
  }
  return subCmd->second.get();
}

void BaseCmd::BlockThisClientToWaitLRPush(std::vector<std::string>& keys, int64_t expire_time, PClient* client,
                                          BlockedConnNode::Type type) {
  std::unique_lock<std::shared_mutex> latch(g_pikiwidb->GetBlockMtx());
  auto& key_to_conns = g_pikiwidb->GetMapFromKeyToConns();
  std::shared_ptr<std::atomic<bool>> is_done = std::make_shared<std::atomic<bool>>(false);
  for (auto key : keys) {
    pikiwidb::BlockKey blpop_key{client->GetCurrentDB(), key};

    auto it = key_to_conns.find(blpop_key);
    if (it == key_to_conns.end()) {
      key_to_conns.emplace(blpop_key, std::make_unique<std::list<BlockedConnNode>>());
      it = key_to_conns.find(blpop_key);
    }
    auto& wait_list_of_this_key = it->second;
    wait_list_of_this_key->emplace_back(expire_time, client, type, is_done);
  }
}

void BaseCmd::ServeAndUnblockConns(PClient* client) {
  pikiwidb::BlockKey key{client->GetCurrentDB(), client->Key()};

  std::shared_lock<std::shared_mutex> read_latch(g_pikiwidb->GetBlockMtx());
  auto& key_to_conns = g_pikiwidb->GetMapFromKeyToConns();
  auto it = key_to_conns.find(key);
  if (it == key_to_conns.end()) {
    // no client is waitting for this key
    return;
  }
  read_latch.unlock();

  std::unique_lock<std::shared_mutex> write_lock(g_pikiwidb->GetBlockMtx());
  auto& waitting_list = it->second;
  std::vector<std::string> elements;
  storage::Status s;

  // traverse this list from head to tail(in the order of adding sequence) ,means "first blocked, first get served“
  for (auto conn_blocked = waitting_list->begin(); conn_blocked != waitting_list->end();) {
    if (conn_blocked->is_done_->exchange(true)) {
      conn_blocked = waitting_list->erase(conn_blocked);
      continue;
    }

    PClient* BlockedClient = (*conn_blocked).GetBlockedClient();

    if (BlockedClient->State() == ClientState::kClosed) {
      conn_blocked = waitting_list->erase(conn_blocked);
      continue;
    }

    switch (conn_blocked->GetCmdType()) {
      case BlockedConnNode::Type::BLPop:
        s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPop(client->Key(), 1, &elements);
        break;
      case BlockedConnNode::Type::BRPop:
        s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPop(client->Key(), 1, &elements);
        break;
    }

    if (s.ok()) {
      BlockedClient->AppendArrayLen(2);
      BlockedClient->AppendString(client->Key());
      BlockedClient->AppendString(elements[0]);
    } else if (s.IsNotFound()) {
      // this key has no more elements to serve more blocked conn.
      break;
    } else {
      BlockedClient->SetRes(CmdRes::kErrOther, s.ToString());
    }
    BlockedClient->SendPacket();
    conn_blocked = waitting_list->erase(conn_blocked);  // remove this conn from current waiting list
  }
}

bool BaseCmdGroup::DoInitial(PClient* client) {
  client->SetSubCmdName(client->argv_[1]);
  if (!subCmds_.contains(client->SubCmdName())) {
    client->SetRes(CmdRes::kErrOther, client->argv_[0] + " unknown subcommand for '" + client->SubCmdName() + "'");
    return false;
  }
  return true;
}

bool BlockedConnNode::IsExpired() {
  if (expire_time_ == 0) {
    return false;
  }
  auto now = std::chrono::system_clock::now();
  int64_t now_in_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
  if (expire_time_ <= now_in_ms) {
    return true;
  }
  return false;
}

}  // namespace pikiwidb
