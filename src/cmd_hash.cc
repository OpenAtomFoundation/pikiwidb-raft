// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of functions for operating key-value pairs
  in a hash table.
 */
#include "cmd_hash.h"

#include <config.h>

#include "pstd/pstd_string.h"
#include "store.h"

namespace pikiwidb {

HSetCmd::HSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HSetCmd::DoInitial(PClient* client) {
  if (client->argv_.size() % 2 != 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameHSet);
    return false;
  }
  client->SetKey(client->argv_[1]);
  client->ClearFvs();
  return true;
}

void HSetCmd::DoCmd(PClient* client) {
  int32_t ret = 0;

  auto fvs = client->Fvs();

  for (size_t i = 2; i < client->argv_.size(); i += 2) {
    auto field = client->argv_[i];
    auto value = client->argv_[i + 1];
    int32_t temp = 0;
    // TODO(century): current bw doesn't support multiple fvs, fix it when necessary
    s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HSet(client->Key(), field, value, &temp);
    if (s_.ok()) {
      ret += temp;
    } else if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      // FIXME(century): need txn, if bw crashes, it should rollback
      client->SetRes(CmdRes::kErrOther);
      return;
    }
  }

  client->AppendInteger(ret);
}

void HSetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HSetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::string field = client->argv_[2];
    std::string value = client->argv_[3];
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HSetIfKeyExist(key, field, value);
  }
}

HGetCmd::HGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HGetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HGetCmd::DoCmd(PClient* client) {
  PString value;
  auto field = client->argv_[2];
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HGet(client->Key(), field, &value);
  if (s_.ok()) {
    client->AppendString(value);
  } else if (s_.IsNotFound()) {
    client->AppendString("");
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "hget cmd error");
  }
}

void HGetCmd::ReadCache(PClient* client) {
  std::string value;
  auto key = client->Key();
  std::string field = client->argv_[2];
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HGet(key, field, &value);
  if (s.ok()) {
    client->AppendStringLen(value.size());
    client->AppendContent(value);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HGetCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HGetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HDelCmd::HDelCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HDelCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HDelCmd::DoCmd(PClient* client) {
  std::vector<std::string> fields(client->argv_.begin() + 2, client->argv_.end());
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HDel(client->Key(), fields, &deleted_);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }
  client->AppendInteger(deleted_);
}

void HDelCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HDelCmd::DoUpdateCache(PClient* client) {
  if (s_.ok() && deleted_ > 0) {
    auto key = client->Key();
    std::vector<std::string> fields(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HDel(key, fields);
  }
}

HMSetCmd::HMSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HMSetCmd::DoInitial(PClient* client) {
  if (client->argv_.size() % 2 != 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameHMSet);
    return false;
  }
  client->SetKey(client->argv_[1]);
  client->ClearFvs();
  // set fvs
  for (size_t index = 2; index < client->argv_.size(); index += 2) {
    client->Fvs().push_back({client->argv_[index], client->argv_[index + 1]});
  }
  return true;
}

void HMSetCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HMSet(client->Key(), client->Fvs());
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void HMSetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HMSetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HMSetxx(key, client->Fvs());
  }
}

HMGetCmd::HMGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HMGetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  client->ClearFields();
  for (size_t i = 2; i < client->argv_.size(); ++i) {
    client->Fields().push_back(client->argv_[i]);
  }
  return true;
}

void HMGetCmd::DoCmd(PClient* client) {
  std::vector<storage::ValueStatus> vss;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HMGet(client->Key(), client->Fields(), &vss);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendArrayLenUint64(vss.size());
    for (size_t i = 0; i < vss.size(); ++i) {
      if (vss[i].status.ok()) {
        client->AppendString(vss[i].value);
      } else {
        client->AppendString("");
      }
    }
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void HMGetCmd::ReadCache(PClient* client) {
  std::vector<storage::ValueStatus> vss;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HMGet(key, client->Fields(), &vss);
  if (s.ok()) {
    client->AppendArrayLen(vss.size());
    for (const auto& vs : vss) {
      if (vs.status.ok()) {
        client->AppendStringLen(vs.value.size());
        client->AppendContent(vs.value);
      } else {
        client->AppendContent("$-1");
      }
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HMGetCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HMGetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HGetAllCmd::HGetAllCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HGetAllCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HGetAllCmd::DoCmd(PClient* client) {
  int64_t total_fv = 0;
  int64_t cursor = 0;
  int64_t next_cursor = 0;
  size_t raw_limit = g_config.max_client_response_size.load();
  std::string raw;
  std::vector<storage::FieldValue> fvs;
  storage::Status s;

  do {
    fvs.clear();
    s = PSTORE.GetBackend(client->GetCurrentDB())
            ->GetStorage()
            ->HScan(client->Key(), cursor, "*", PIKIWIDB_SCAN_STEP_LENGTH, &fvs, &next_cursor);
    if (!s.ok()) {
      raw.clear();
      total_fv = 0;
      break;
    } else {
      for (const auto& fv : fvs) {
        client->RedisAppendLenUint64(raw, fv.field.size(), "$");
        client->RedisAppendContent(raw, fv.field);
        client->RedisAppendLenUint64(raw, fv.value.size(), "$");
        client->RedisAppendContent(raw, fv.value);
      }
      if (raw.size() >= raw_limit) {
        client->SetRes(CmdRes::kErrOther, "Response exceeds the max-client-response-size limit");
        return;
      }
      total_fv += static_cast<int64_t>(fvs.size());
      cursor = next_cursor;
    }
  } while (cursor != 0);

  if (s.ok() || s.IsNotFound()) {
    client->AppendArrayLen(total_fv * 2);
    client->AppendStringRaw(raw);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HGetAllCmd::ReadCache(PClient* client) {
  std::vector<storage::FieldValue> fvs;
  auto key = client->Key();
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HGetall(key, &fvs);
  if (s_.ok()) {
    client->AppendArrayLen(fvs.size() * 2);
    for (const auto& fv : fvs) {
      client->AppendStringLen(fv.field.size());
      client->AppendContent(fv.field);
      client->AppendStringLen(fv.value.size());
      client->AppendContent(fv.value);
    }
  } else if (s_.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void HGetAllCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HGetAllCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HKeysCmd::HKeysCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HKeysCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HKeysCmd::DoCmd(PClient* client) {
  std::vector<std::string> fields;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HKeys(client->Key(), &fields);
  if (s.ok() || s.IsNotFound()) {
    client->AppendArrayLenUint64(fields.size());
    for (const auto& field : fields) {
      client->AppendStringLenUint64(field.size());
      client->AppendContent(field);
    }
    // update fields
    client->Fields() = std::move(fields);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HKeysCmd::ReadCache(PClient* client) {
  std::vector<std::string> fields;
  auto key = client->Key();
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HKeys(key, &fields);
  if (s_.ok()) {
    client->AppendArrayLen(fields.size());
    for (const auto& field : fields) {
      client->AppendString(field);
    }
  } else if (s_.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void HKeysCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HKeysCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HLenCmd::HLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HLenCmd::DoCmd(PClient* client) {
  int32_t len = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HLen(client->Key(), &len);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(len);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hlen");
  }
}

void HLenCmd::ReadCache(PClient* client) {
  uint64_t len = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HLen(key, &len);
  if (s.ok()) {
    client->AppendInteger(len);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hlen");
  }
}

void HLenCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HLenCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HStrLenCmd::HStrLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HStrLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HStrLenCmd::DoCmd(PClient* client) {
  int32_t len = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HStrlen(client->Key(), client->argv_[2], &len);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(len);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hstrlen");
  }
}

void HStrLenCmd::ReadCache(PClient* client) {
  uint64_t len = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HStrlen(key, client->argv_[2], &len);
  if (s.ok()) {
    client->AppendInteger(len);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hstrlen");
  }
  return;
}

void HStrLenCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HStrLenCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HScanCmd::HScanCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HScanCmd::DoInitial(PClient* client) {
  if (auto size = client->argv_.size(); size != 3 && size != 5 && size != 7) {
    client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void HScanCmd::DoCmd(PClient* client) {
  const auto& argv = client->argv_;
  // parse arguments
  int64_t cursor{};
  int64_t count{10};
  std::string pattern{"*"};
  if (pstd::String2int(argv[2], &cursor) == 0) {
    client->SetRes(CmdRes::kInvalidCursor, kCmdNameHScan);
    return;
  }
  for (size_t i = 3; i < argv.size(); i += 2) {
    if (auto lower = pstd::StringToLower(argv[i]); kMatchSymbol == lower) {
      pattern = argv[i + 1];
    } else if (kCountSymbol == lower) {
      if (pstd::String2int(argv[i + 1], &count) == 0) {
        client->SetRes(CmdRes::kInvalidInt, kCmdNameHScan);
        return;
      }
      if (count < 0) {
        client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
        return;
      }
    } else {
      client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
      return;
    }
  }

  // execute command
  std::vector<storage::FieldValue> fvs;
  int64_t next_cursor{};
  auto status = PSTORE.GetBackend(client->GetCurrentDB())
                    ->GetStorage()
                    ->HScan(client->Key(), cursor, pattern, count, &fvs, &next_cursor);
  if (!status.ok() && !status.IsNotFound()) {
    if (status.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, status.ToString());
    }
    return;
  }

  // reply to client
  client->AppendArrayLen(2);
  client->AppendString(std::to_string(next_cursor));
  client->AppendArrayLenUint64(fvs.size() * 2);
  for (const auto& [field, value] : fvs) {
    client->AppendString(field);
    client->AppendString(value);
  }
}

HValsCmd::HValsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HValsCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HValsCmd::DoCmd(PClient* client) {
  std::vector<std::string> valueVec;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HVals(client->Key(), &valueVec);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendStringVector(valueVec);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "hvals cmd error");
  }
}

void HValsCmd::ReadCache(PClient* client) {
  std::vector<std::string> values;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HVals(key, &values);
  if (s.ok()) {
    client->AppendArrayLen(values.size());
    for (const auto& value : values) {
      client->AppendStringLen(value.size());
      client->AppendContent(value);
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HValsCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HValsCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

HIncrbyFloatCmd::HIncrbyFloatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HIncrbyFloatCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  long double long_double_by = 0;
  if (-1 == StrToLongDouble(client->argv_[3].c_str(), static_cast<int>(client->argv_[3].size()), &long_double_by)) {
    client->SetRes(CmdRes::kInvalidParameter);
    return false;
  }
  return true;
}

void HIncrbyFloatCmd::DoCmd(PClient* client) {
  long double long_double_by = 0;
  if (-1 == StrToLongDouble(client->argv_[3].c_str(), static_cast<int>(client->argv_[3].size()), &long_double_by)) {
    client->SetRes(CmdRes::kInvalidFloat);
    return;
  }
  std::string newValue;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->HIncrbyfloat(client->Key(), client->argv_[2], client->argv_[3], &newValue);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendString(newValue);
  } else if (s_.IsInvalidArgument() &&
             s_.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: value is not a valid float") {
    client->SetRes(CmdRes::kInvalidFloat);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, "hvals cmd error");
  }
}

void HIncrbyFloatCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HIncrbyFloatCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    long double long_double_by;
    if (StrToLongDouble(client->argv_[3].c_str(), static_cast<int>(client->argv_[3].size()), &long_double_by) != -1) {
      auto key = client->Key();
      PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HIncrbyfloatxx(key, client->argv_[2], long_double_by);
    }
  }
}

HSetNXCmd::HSetNXCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HSetNXCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HSetNXCmd::DoCmd(PClient* client) {
  int32_t temp = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->HSetnx(client->Key(), client->argv_[2], client->argv_[3], &temp);
  if (s_.ok()) {
    client->AppendInteger(temp);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "hsetnx cmd error");
  }
  return;
}

void HSetNXCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HSetNXCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())
        ->GetCache()
        ->HSetIfKeyExistAndFieldNotExist(key, client->argv_[2], client->argv_[3]);
  }
}

HIncrbyCmd::HIncrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB,
              kAclCategoryWrite | kAclCategoryHash) {}

bool HIncrbyCmd::DoInitial(PClient* client) {
  if (!pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &int_by_)) {
    client->SetRes(CmdRes::kInvalidParameter);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void HIncrbyCmd::DoCmd(PClient* client) {
  int64_t temp = 0;
  s_ =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HIncrby(client->Key(), client->argv_[2], int_by_, &temp);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(temp);
  } else if (s_.IsInvalidArgument() &&
             s_.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else if (s_.IsCorruption() && s_.ToString() == "Corruption: hash value is not an integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, "hincrby cmd error");
  }
}

void HIncrbyCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void HIncrbyCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HIncrbyxx(key, client->argv_[2], int_by_);
  }
}

HRandFieldCmd::HRandFieldCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HRandFieldCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HRandFieldCmd::DoCmd(PClient* client) {
  // parse arguments
  const auto& argv = client->argv_;
  int64_t count{1};
  bool with_values{false};
  if (argv.size() > 2) {
    // redis checks the integer argument first and then the number of parameters
    if (pstd::String2int(argv[2], &count) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }
    if (argv.size() > 4) {
      client->SetRes(CmdRes::kSyntaxErr);
      return;
    }
    if (argv.size() > 3) {
      if (kWithValueString != pstd::StringToLower(argv[3])) {
        client->SetRes(CmdRes::kSyntaxErr);
        return;
      }
      with_values = true;
    }
  }

  // execute command
  std::vector<std::string> res;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HRandField(client->Key(), count, with_values, &res);
  if (s.IsNotFound()) {
    client->AppendString("");
    return;
  }
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
    return;
  }

  // reply to client
  if (argv.size() > 2) {
    client->AppendArrayLenUint64(res.size());
  }
  for (const auto& item : res) {
    client->AppendString(item);
  }
}

HExistsCmd::HExistsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsHash | kCmdFlagsUpdateCache | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryHash) {}

bool HExistsCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HExistsCmd::DoCmd(PClient* client) {
  // parse arguments
  auto& field = client->argv_[2];

  // execute command
  std::vector<std::string> res;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HExists(client->Key(), field);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }

  // reply
  client->AppendInteger(s_.IsNotFound() ? 0 : 1);
}

void HExistsCmd::ReadCache(PClient* client) {
  auto key = client->Key();
  auto field = client->argv_[2];
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->HExists(key, field);
  if (s.ok()) {
    client->AppendContent(":1");
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void HExistsCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void HExistsCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_HASH, key, client);
  }
}

}  // namespace pikiwidb
