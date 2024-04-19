/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_admin.h"
#include "pikiwidb.h"
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

}  // namespace pikiwidb
