/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_keys.h"

#include "pstd_string.h"

#include "store.h"

namespace pikiwidb {

DelCmd::DelCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool DelCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin() + 1, client->argv_.end());
  client->SetKey(keys);
  return true;
}

void DelCmd::DoCmd(PClient* client) {
  int64_t count = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Del(client->Keys());
  if (count >= 0) {
    client->AppendInteger(count);
  } else {
    client->SetRes(CmdRes::kErrOther, "delete error");
  }
}

ExistsCmd::ExistsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryKeyspace) {}

bool ExistsCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin() + 1, client->argv_.end());
  client->SetKey(keys);
  return true;
}

void ExistsCmd::DoCmd(PClient* client) {
  int64_t count = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Exists(client->Keys());
  if (count >= 0) {
    client->AppendInteger(count);
    //    if (PSTORE.ExistsKey(client->Key())) {
    //      client->AppendInteger(1);
    //    } else {
    //      client->SetRes(CmdRes::kErrOther, "exists internal error");
    //    }
  } else {
    client->SetRes(CmdRes::kErrOther, "exists internal error");
  }
}

TypeCmd::TypeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryKeyspace) {}

bool TypeCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void TypeCmd::DoCmd(PClient* client) {
  storage::DataType type = storage::DataType::kNones;
  rocksdb::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->GetType(client->Key(), type);
  if (s.ok()) {
    client->AppendContent("+" + std::string(storage::DataTypeToString(type)));
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

ExpireCmd::ExpireCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool ExpireCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ExpireCmd::DoCmd(PClient* client) {
  uint64_t sec = 0;
  if (pstd::String2int(client->argv_[2], &sec) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return;
  }
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expire(client->Key(), sec);
  if (res != -1) {
    client->AppendInteger(res);
  } else {
    client->SetRes(CmdRes::kErrOther, "expire internal error");
  }
}

TtlCmd::TtlCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryKeyspace) {}

bool TtlCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void TtlCmd::DoCmd(PClient* client) {
  auto timestamp = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->TTL(client->Key());
  if (timestamp == -3) {
    client->SetRes(CmdRes::kErrOther, "ttl internal error");
  } else {
    client->AppendInteger(timestamp);
  }
}

PExpireCmd::PExpireCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PExpireCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void PExpireCmd::DoCmd(PClient* client) {
  int64_t msec = 0;
  if (pstd::String2int(client->argv_[2], &msec) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return;
  }
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expire(client->Key(), msec / 1000);
  if (res != -1) {
    client->AppendInteger(res);
  } else {
    client->SetRes(CmdRes::kErrOther, "pexpire internal error");
  }
}

ExpireatCmd::ExpireatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool ExpireatCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ExpireatCmd::DoCmd(PClient* client) {
  int64_t time_stamp = 0;
  if (pstd::String2int(client->argv_[2], &time_stamp) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return;
  }
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expireat(client->Key(), time_stamp);
  if (res != -1) {
    client->AppendInteger(res);
  } else {
    client->SetRes(CmdRes::kErrOther, "expireat internal error");
  }
}

PExpireatCmd::PExpireatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PExpireatCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

// PExpireatCmd actually invoke Expireat
void PExpireatCmd::DoCmd(PClient* client) {
  int64_t time_stamp_ms = 0;
  if (pstd::String2int(client->argv_[2], &time_stamp_ms) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return;
  }
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expireat(client->Key(), time_stamp_ms / 1000);
  if (res != -1) {
    client->AppendInteger(res);
  } else {
    client->SetRes(CmdRes::kErrOther, "pexpireat internal error");
  }
}

PersistCmd::PersistCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PersistCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void PersistCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Persist(client->Key());
  if (res != -1) {
    client->AppendInteger(res);
  } else {
    client->SetRes(CmdRes::kErrOther, "persist internal error");
  }
}

KeysCmd::KeysCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryKeyspace) {}

bool KeysCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void KeysCmd::DoCmd(PClient* client) {
  std::vector<std::string> keys;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Keys(storage::DataType::kAll, client->Key(), &keys);
  if (s.ok()) {
    client->AppendArrayLen(keys.size());
    for (auto k : keys) {
      client->AppendString(k);
    }
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

PttlCmd::PttlCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryKeyspace) {}

bool PttlCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

// like Blackwidow , Floyd still possible has same key in different data structure
void PttlCmd::DoCmd(PClient* client) {
  auto timestamp = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->TTL(client->Key());
  // mean operation exception errors happen in database
  if (timestamp == -3) {
    client->SetRes(CmdRes::kErrOther, "ttl internal error");
  } else {
    client->AppendInteger(timestamp);
  }
}

RenameCmd::RenameCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool RenameCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RenameCmd::DoCmd(PClient* client) {
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Rename(client->Key(), client->argv_[2]);
  if (s.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kNotFound, s.ToString());
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

RenameNXCmd::RenameNXCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryKeyspace) {}

bool RenameNXCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RenameNXCmd::DoCmd(PClient* client) {
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Renamenx(client->Key(), client->argv_[2]);
  if (s.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kNotFound, s.ToString());
  } else if (s.IsCorruption()) {
    client->AppendInteger(0);  // newkey already exists
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

}  // namespace pikiwidb
