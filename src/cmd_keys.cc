// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  This file implements commands that focus on the keys in
  key-value pairs, rather than the values.
 */

#include "cmd_keys.h"

#include "pstd_string.h"

#include "store.h"

namespace pikiwidb {

DelCmd::DelCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}
bool DelCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin() + 1, client->argv_.end());
  client->SetKey(keys);
  return true;
}

void DelCmd::DoCmd(PClient* client) {
  int64_t count = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Del(client->Keys());
  if (count >= 0) {
    client->AppendInteger(count);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "delete error");
    s_ = rocksdb::Status::Corruption("delete error");
  }
}

void DelCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void DelCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v(client->Keys());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
  }
}

ExistsCmd::ExistsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryKeyspace) {}

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

void ExistsCmd::ReadCache(PClient* client) {
  auto keys = client->Keys();
  if (1 < keys.size()) {
    client->SetRes(CmdRes::kCacheMiss);
    return;
  }
  bool exist = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Exists(keys[0]);
  if (exist) {
    client->AppendInteger(1);
  } else {
    client->SetRes(CmdRes::kCacheMiss);
  }
}

void ExistsCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

TypeCmd::TypeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryKeyspace) {}

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

void TypeCmd::ReadCache(PClient* client) {
  std::string key_type;
  auto key = client->Key();
  rocksdb::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Type(key, &key_type);
  if (s.ok()) {
    client->AppendContent(key_type);
  } else {
    client->SetRes(CmdRes::kCacheMiss, s.ToString());
  }
}

void TypeCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

ExpireCmd::ExpireCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool ExpireCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &sec_) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ExpireCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expire(client->Key(), sec_);
  if (res != -1) {
    client->AppendInteger(res);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "expire internal error");
    s_ = rocksdb::Status::Corruption("expire internal error");
  }
}

void ExpireCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ExpireCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Expire(key, sec_);
  }
}

TtlCmd::TtlCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryKeyspace) {}

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

void TtlCmd::ReadCache(PClient* client) {
  rocksdb::Status s;
  auto key = client->Key();
  auto timestamp = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->TTL(key);
  if (timestamp == PCache_TTL_FAILED) {
    client->SetRes(CmdRes::kErrOther, "ttl internal error");
    return;
  }
  if (timestamp == PCache_KEY_NOT_FOUND) {
    // mean this key not exist
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->AppendInteger(timestamp);
  }
}

void TtlCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

PExpireCmd::PExpireCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PExpireCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &msec_) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void PExpireCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expire(client->Key(), msec_ / 1000);
  if (res != -1) {
    client->AppendInteger(res);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "pexpire internal error");
    s_ = rocksdb::Status::Corruption("pexpire internal error");
  }
}

void PExpireCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void PExpireCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Expire(key, msec_ / 1000);
  }
}

ExpireatCmd::ExpireatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool ExpireatCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &time_stamp_) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ExpireatCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expireat(client->Key(), time_stamp_);
  if (res != -1) {
    client->AppendInteger(res);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "expireat internal error");
    s_ = rocksdb::Status::Corruption("expireat internal error");
  }
}

void ExpireatCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ExpireatCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Expireat(key, time_stamp_);
  }
}

PExpireatCmd::PExpireatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PExpireatCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &time_stamp_ms_) == 0) {
    client->SetRes(CmdRes ::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

// PExpireatCmd actually invoke Expireat
void PExpireatCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Expireat(client->Key(), time_stamp_ms_ / 1000);
  if (res != -1) {
    client->AppendInteger(res);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "pexpireat internal error");
    s_ = rocksdb::Status::Corruption("pexpireat internal error");
  }
}

void PExpireatCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void PExpireatCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Expireat(key, time_stamp_ms_ / 1000);
  }
}

PersistCmd::PersistCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool PersistCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void PersistCmd::DoCmd(PClient* client) {
  auto res = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Persist(client->Key());
  if (res != -1) {
    client->AppendInteger(res);
    s_ = rocksdb::Status::OK();
  } else {
    client->SetRes(CmdRes::kErrOther, "persist internal error");
    s_ = rocksdb::Status::Corruption("persist internal error");
  }
}

void PersistCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void PersistCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Persist(key);
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
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryKeyspace) {}

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
    client->AppendInteger(timestamp * 1000);
  }
}

void PttlCmd::ReadCache(PClient* client) {
  rocksdb::Status s;
  auto key = client->Key();
  auto timestamp = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->TTL(key);
  if (timestamp == PCache_TTL_FAILED) {
    client->SetRes(CmdRes::kErrOther, "ttl internal error");
    return;
  }
  if (timestamp == PCache_KEY_NOT_FOUND) {
    // mean this key not exist
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->AppendInteger(timestamp * 1000);
  }
}

void PttlCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

RenameCmd::RenameCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryKeyspace) {}

bool RenameCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RenameCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Rename(client->Key(), client->argv_[2]);
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsNotFound()) {
    client->SetRes(CmdRes::kNotFound, s_.ToString());
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void RenameCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void RenameCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v;
    v.emplace_back(client->Key());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
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
