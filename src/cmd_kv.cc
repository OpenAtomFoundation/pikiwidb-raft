// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Implemented a set of operation functions and commands
  associated with key-value pairs.
 */

#include "cmd_kv.h"
#include "common.h"
#include "pstd_string.h"
#include "pstd_util.h"
#include "store.h"

namespace pikiwidb {

GetCmd::GetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryString) {}

bool GetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->GetWithTTL(client->Key(), &value_, &ttl_);
  if (s_.ok()) {
    client->AppendString(value_);
  } else if (s_.IsNotFound()) {
    client->AppendString("");
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "get key error");
  }
}

void GetCmd::ReadCache(PClient* client) {
  auto key_ = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Get(key_, &value_);
  if (s.ok()) {
    client->AppendString(value_);
  } else {
    client->SetRes(CmdRes::kCacheMiss);
  }
}

void GetCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void GetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->WriteKVToCache(key_, value_, ttl_);
  }
}

SetCmd::SetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

// SET key value [NX | XX] [EX seconds | PX milliseconds]
bool SetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  auto argv_ = client->argv_;
  value_ = argv_[2];
  condition_ = SetCmd::kNONE;
  sec_ = 0;
  size_t index = 3;

  while (index != argv_.size()) {
    std::string opt = argv_[index];
    if (strcasecmp(opt.data(), "xx") == 0) {
      condition_ = SetCmd::kXX;
    } else if (strcasecmp(opt.data(), "nx") == 0) {
      condition_ = SetCmd::kNX;
    } else if ((strcasecmp(opt.data(), "ex") == 0) || (strcasecmp(opt.data(), "px") == 0)) {
      condition_ = (condition_ == SetCmd::kNONE) ? SetCmd::kEXORPX : condition_;
      index++;
      if (index == argv_.size()) {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      if (pstd::String2int(argv_[index].data(), argv_[index].size(), &sec_) == 0) {
        client->SetRes(CmdRes::kInvalidInt);
        return false;
      }

      if (strcasecmp(opt.data(), "px") == 0) {
        sec_ /= 1000;
      }
      has_ttl_ = true;
    } else {
      client->SetRes(CmdRes::kSyntaxErr);
      return false;
    }
    index++;
  }

  return true;
}

void SetCmd::DoCmd(PClient* client) {
  int32_t res = 1;
  auto key_ = client->Key();
  switch (condition_) {
    case SetCmd::kXX:
      s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setxx(key_, value_, &res, sec_);
      break;
    case SetCmd::kNX:
      s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setnx(key_, value_, &res, sec_);
      break;
    case SetCmd::kEXORPX:
      s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setex(key_, value_, sec_);
      break;
    default:
      s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Set(key_, value_);
      break;
  }

  if (s_.ok() || s_.IsNotFound()) {
    if (res == 1) {
      client->SetRes(CmdRes::kOK);
    } else {
      client->AppendStringLen(-1);
    }
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void SetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void SetCmd::DoUpdateCache(PClient* client) {
  if (SetCmd::kNX == condition_) {
    return;
  }
  auto key_ = client->Key();
  if (s_.ok()) {
    if (has_ttl_) {
      PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Setxx(key_, value_, sec_);
    } else {
      PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->SetxxWithoutTTL(key_, value_);
    }
  }
}

AppendCmd::AppendCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool AppendCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void AppendCmd::DoCmd(PClient* client) {
  int32_t new_len = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Append(client->Key(), client->argv_[2], &new_len);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(new_len);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void AppendCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void AppendCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Appendxx(key_, client->argv_[2]);
  }
}

GetSetCmd::GetSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool GetSetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetSetCmd::DoCmd(PClient* client) {
  std::string old_value;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->GetSet(client->Key(), client->argv_[2], &old_value);
  if (s_.ok()) {
    if (old_value.empty()) {
      client->AppendContent("$-1");
    } else {
      client->AppendStringLen(old_value.size());
      client->AppendContent(old_value);
    }
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void GetSetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void GetSetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->SetxxWithoutTTL(key_, client->argv_[2]);
  }
}

MGetCmd::MGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryString) {}

bool MGetCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin(), client->argv_.end());
  keys.erase(keys.begin());
  client->SetKey(keys);
  return true;
}

void MGetCmd::DoCmd(PClient* client) {
  db_value_status_array_.clear();
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->MGet(client->Keys(), &db_value_status_array_);
  if (s_.ok()) {
    client->AppendArrayLen(db_value_status_array_.size());
    for (const auto& vs : db_value_status_array_) {
      if (vs.status.ok()) {
        client->AppendStringLen(vs.value.size());
        client->AppendContent(vs.value);
      } else {
        client->AppendContent("$-1");
      }
    }
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void MGetCmd::ReadCache(PClient* client) {
  auto keys_ = client->Keys();
  if (1 < keys_.size()) {
    client->SetRes(CmdRes::kCacheMiss);
    return;
  }
  std::string value;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Get(keys_[0], &value);
  if (s.ok()) {
    client->AppendArrayLen(1);
    client->AppendStringLen(value.size());
    client->AppendContent(value);
  } else {
    client->SetRes(CmdRes::kCacheMiss);
  }
}

void MGetCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void MGetCmd::DoUpdateCache(PClient* client) {
  auto keys_ = client->Keys();
  for (size_t i = 0; i < keys_.size(); i++) {
    if (db_value_status_array_[i].status.ok()) {
      PSTORE.GetBackend(client->GetCurrentDB())
          ->GetCache()
          ->WriteKVToCache(keys_[i], db_value_status_array_[i].value, db_value_status_array_[i].ttl);
    }
  }
}

MSetCmd::MSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool MSetCmd::DoInitial(PClient* client) {
  size_t argcSize = client->argv_.size();
  if (argcSize % 2 == 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameMSet);
    return false;
  }
  kvs_.clear();
  for (size_t index = 1; index != argcSize; index += 2) {
    kvs_.push_back({client->argv_[index], client->argv_[index + 1]});
  }
  return true;
}

void MSetCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->MSet(kvs_);
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void MSetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void MSetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    for (auto key : kvs_) {
      PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->SetxxWithoutTTL(key.key, key.value);
    }
  }
}

BitCountCmd::BitCountCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool BitCountCmd::DoInitial(PClient* client) {
  size_t paramSize = client->argv_.size();
  if (paramSize != 2 && paramSize != 4) {
    client->SetRes(CmdRes::kSyntaxErr, kCmdNameBitCount);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void BitCountCmd::DoCmd(PClient* client) {
  storage::Status s;
  int32_t count = 0;
  if (client->argv_.size() == 2) {
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->BitCount(client->Key(), 0, 0, &count, false);
  } else {
    int64_t start_offset = 0;
    int64_t end_offset = 0;
    if (pstd::String2int(client->argv_[2], &start_offset) == 0 ||
        pstd::String2int(client->argv_[3], &end_offset) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }

    s = PSTORE.GetBackend(client->GetCurrentDB())
            ->GetStorage()
            ->BitCount(client->Key(), start_offset, end_offset, &count, true);
  }

  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(count);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

DecrCmd::DecrCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategoryString) {}

bool DecrCmd::DoInitial(pikiwidb::PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void DecrCmd::DoCmd(pikiwidb::PClient* client) {
  int64_t ret = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Decrby(client->Key(), 1, &ret);
  if (s_.ok()) {
    client->AppendContent(":" + std::to_string(ret));
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: Value is not a integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void DecrCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void DecrCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Decrxx(key_);
  }
}

IncrCmd::IncrCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool IncrCmd::DoInitial(pikiwidb::PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void IncrCmd::DoCmd(pikiwidb::PClient* client) {
  int64_t ret = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Incrby(client->Key(), 1, &ret);
  if (s_.ok()) {
    client->AppendContent(":" + std::to_string(ret));
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: Value is not a integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void IncrCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void IncrCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Incrxx(key_);
  }
}

BitOpCmd::BitOpCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool BitOpCmd::DoInitial(PClient* client) {
  if (!(pstd::StringEqualCaseInsensitive(client->argv_[1], "and") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "or") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "not") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "xor"))) {
    client->SetRes(CmdRes::kSyntaxErr, "operation error");
    return false;
  }
  return true;
}

void BitOpCmd::DoCmd(PClient* client) {
  std::vector<std::string> keys;
  for (size_t i = 3; i < client->argv_.size(); ++i) {
    keys.push_back(client->argv_[i]);
  }

  PError err = kPErrorParam;
  PString res;
  storage::BitOpType op = storage::kBitOpDefault;

  if (!keys.empty()) {
    if (pstd::StringEqualCaseInsensitive(client->argv_[1], "or")) {
      err = kPErrorOK;
      op = storage::kBitOpOr;
    } else if (pstd::StringEqualCaseInsensitive(client->argv_[1], "xor")) {
      err = kPErrorOK;
      op = storage::kBitOpXor;
    } else if (pstd::StringEqualCaseInsensitive(client->argv_[1], "and")) {
      err = kPErrorOK;
      op = storage::kBitOpAnd;
    } else if (pstd::StringEqualCaseInsensitive(client->argv_[1], "not")) {
      if (keys.size() == 1) {
        err = kPErrorOK;
        op = storage::kBitOpNot;
      }
    }
  }

  if (err != kPErrorOK) {
    client->SetRes(CmdRes::kSyntaxErr);
  } else {
    PString value;
    int64_t result_length = 0;
    storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                            ->GetStorage()
                            ->BitOp(op, client->argv_[2], keys, value, &result_length);
    if (s.ok()) {
      client->AppendInteger(result_length);
    } else if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
  }
}

StrlenCmd::StrlenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryString) {}

bool StrlenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void StrlenCmd::DoCmd(PClient* client) {
  int32_t len = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Strlen(client->Key(), &len);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(len);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void StrlenCmd::ReadCache(PClient* client) {
  int32_t len = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Strlen(key, &len);
  if (s.ok()) {
    client->AppendInteger(len);
  } else {
    client->SetRes(CmdRes::kCacheMiss);
  }
}

void StrlenCmd::DoThroughDB(PClient* client) {
  client->Clear();
  auto key = client->Key();
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->GetWithTTL(key, &value_, &sec_);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(value_.size());
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void StrlenCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->WriteKVToCache(key, value_, sec_);
  }
}

SetExCmd::SetExCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool SetExCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  if (pstd::String2int(client->argv_[2], &sec_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  return true;
}

void SetExCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setex(client->Key(), client->argv_[3], sec_);
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void SetExCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void SetExCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Setxx(key, client->argv_[3], sec_);
  }
}

PSetExCmd::PSetExCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool PSetExCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);

  if (pstd::String2int(client->argv_[2], &msec_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  return true;
}

void PSetExCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->Setex(client->Key(), client->argv_[3], static_cast<int32_t>(msec_ / 1000));
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void PSetExCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void PSetExCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Setxx(key, client->argv_[3], msec_ / 1000);
  }
}

IncrbyCmd::IncrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool IncrbyCmd::DoInitial(PClient* client) {
  int64_t by_ = 0;
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by_))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void IncrbyCmd::DoCmd(PClient* client) {
  int64_t ret = 0;
  pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by_);
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Incrby(client->Key(), by_, &ret);
  if (s_.ok()) {
    client->AppendContent(":" + std::to_string(ret));
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: Value is not a integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else if (s_.IsInvalidArgument() &&
             s_.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  };
}

void IncrbyCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void IncrbyCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->IncrByxx(key_, by_);
  }
}

DecrbyCmd::DecrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool DecrbyCmd::DoInitial(PClient* client) {
  int64_t by = 0;
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void DecrbyCmd::DoCmd(PClient* client) {
  int64_t ret = 0;

  if (pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Decrby(client->Key(), by_, &ret);
  if (s_.ok()) {
    client->AppendContent(":" + std::to_string(ret));
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: Value is not a integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else if (s_.IsInvalidArgument() &&
             s_.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void DecrbyCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void DecrbyCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key_ = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->DecrByxx(key_, by_);
  }
}

IncrbyFloatCmd::IncrbyFloatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool IncrbyFloatCmd::DoInitial(PClient* client) {
  long double by_ = 0.00f;
  if (pikiwidb::StrToLongDouble(client->argv_[2].data(), client->argv_[2].size(), &by_)) {
    client->SetRes(CmdRes::kInvalidFloat);
    return false;
  }
  client->SetKey(client->argv_[1]);
  value_ = client->argv_[2];
  return true;
}

void IncrbyFloatCmd::DoCmd(PClient* client) {
  PString ret;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Incrbyfloat(client->Key(), value_, &ret);
  if (s_.ok()) {
    client->AppendStringLen(ret.size());
    client->AppendContent(ret);
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: Value is not a valid float") {
    client->SetRes(CmdRes::kInvalidFloat);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::KIncrByOverFlow);
  } else if (s_.IsInvalidArgument() &&
             s_.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void IncrbyFloatCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void IncrbyFloatCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    long double long_double_by;
    if (StrToLongDouble(value_.data(), value_.size(), &long_double_by) != -1) {
      auto key_ = client->Key();
      PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Incrbyfloatxx(key_, long_double_by);
    }
  }
}

SetNXCmd::SetNXCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetNXCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SetNXCmd::DoCmd(PClient* client) {
  int32_t success = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setnx(client->Key(), client->argv_[2], &success);
  if (s.ok()) {
    client->AppendInteger(success);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

GetBitCmd::GetBitCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsFast, kAclCategoryRead | kAclCategoryBitmap) {}

bool GetBitCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetBitCmd::DoCmd(PClient* client) {
  int32_t bit_val = 0;
  long offset = 0;
  if (!pstd::String2int(client->argv_[2].c_str(), client->argv_[2].size(), &offset)) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->GetBit(client->Key(), offset, &bit_val);
  if (s.ok()) {
    client->AppendInteger(bit_val);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

GetRangeCmd::GetRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryString) {}

bool GetRangeCmd::DoInitial(PClient* client) {
  // > range key start end
  int64_t start = 0;
  int64_t end = 0;
  // ERR value is not an integer or out of range
  if (pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &start_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  if (pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &end_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void GetRangeCmd::DoCmd(PClient* client) {
  PString ret;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Getrange(client->Key(), start_, end_, &ret);
  if (!s_.ok()) {
    if (s_.IsNotFound()) {
      client->AppendString("");
    } else if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "getrange cmd error");
    }
    return;
  }
  client->AppendString(ret);
}

void GetRangeCmd::ReadCache(PClient* client) {
  std::string substr;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->GetRange(key, start_, end_, &substr);
  if (s.ok()) {
    client->AppendStringLen(substr.size());
    client->AppendContent(substr);
  } else {
    client->SetRes(CmdRes::kCacheMiss);
  }
}

void GetRangeCmd::DoThroughDB(PClient* client) {
  client->Clear();
  std::string substr;
  auto key = client->Key();
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->GetrangeWithValue(key, start_, end_, &substr, &value_, &sec_);
  if (s_.ok()) {
    client->AppendStringLen(substr.size());
    client->AppendContent(substr);
  } else if (s_.IsNotFound()) {
    client->AppendStringLen(substr.size());
    client->AppendContent(substr);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void GetRangeCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->WriteKVToCache(key, value_, sec_);
  }
}

SetBitCmd::SetBitCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetBitCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SetBitCmd::DoCmd(PClient* client) {
  long offset = 0;
  long on = 0;
  if (pstd::String2int(client->argv_[2].c_str(), client->argv_[2].size(), &offset) == 0) {
    client->SetRes(CmdRes::kInvalidBitOffsetInt);
    return;
  }
  if (pstd::String2int(client->argv_[3].c_str(), client->argv_[3].size(), &on) == 0) {
    client->SetRes(CmdRes::kInvalidBitInt);
    return;
  }

  if (offset < 0 || offset > kStringMaxBytes) {
    client->SetRes(CmdRes::kInvalidBitInt);
    return;
  }

  if ((on & ~1) != 0) {
    client->SetRes(CmdRes::kInvalidBitInt);
    return;
  }

  PString value;
  int32_t bit_val = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->SetBit(client->Key(), offset, static_cast<int32_t>(on), &bit_val);
  if (s.ok()) {
    client->AppendInteger(static_cast<int>(bit_val));
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

SetRangeCmd::SetRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsKv | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool SetRangeCmd::DoInitial(PClient* client) {
  // setrange key offset value
  client->SetKey(client->argv_[1]);
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &offset_))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  return true;
}

void SetRangeCmd::DoCmd(PClient* client) {
  int32_t ret = 0;
  s_ =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Setrange(client->Key(), offset_, client->argv_[3], &ret);
  if (!s_.ok()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "setrange cmd error");
    }
    return;
  }
  client->AppendInteger(static_cast<int>(ret));
}

void SetRangeCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void SetRangeCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->SetRangexx(key, offset_, client->argv_[3]);
  }
}

MSetnxCmd::MSetnxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool MSetnxCmd::DoInitial(PClient* client) {
  size_t argcSize = client->argv_.size();
  if (argcSize % 2 == 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameMSetnx);
    return false;
  }
  std::vector<std::string> keys;
  for (size_t index = 1; index < argcSize; index += 2) {
    keys.emplace_back(client->argv_[index]);
  }
  client->SetKey(keys);
  return true;
}

void MSetnxCmd::DoCmd(PClient* client) {
  int32_t success = 0;
  std::vector<storage::KeyValue> kvs;
  for (size_t index = 1; index != client->argv_.size(); index += 2) {
    kvs.push_back({client->argv_[index], client->argv_[index + 1]});
  }
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->MSetnx(kvs, &success);
  if (s.ok()) {
    client->AppendInteger(success);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}
}  // namespace pikiwidb
