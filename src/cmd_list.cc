// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Implemented a set of operation functions and commands related to lists.
 */

#include "cmd_list.h"
#include "pstd_string.h"
#include "store.h"

namespace pikiwidb {
LPushCmd::LPushCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LPushCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPushCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPush(client->Key(), list_values, &reply_num);
  if (s_.ok()) {
    client->AppendInteger(reply_num);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "lpush cmd error");
  }
}

void LPushCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LPushCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LPushx(key, list_values);
  }
}

LPushxCmd::LPushxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LPushxCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPushxCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPushx(client->Key(), list_values, &reply_num);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void LPushxCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LPushxCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LPushx(key, list_values);
  }
}

RPoplpushCmd::RPoplpushCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool RPoplpushCmd::DoInitial(PClient* client) {
  if (((arity_ > 0 && client->argv_.size() != arity_) || (arity_ < 0 && client->argv_.size() < -arity_))) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameRPoplpush);
    return false;
  }
  source_ = client->argv_[1];
  receiver_ = client->argv_[2];
  return true;
}

void RPoplpushCmd::DoCmd(PClient* client) {
  std::string value;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPoplpush(source_, receiver_, &value);
  if (s.ok()) {
    client->AppendString(value);
  } else if (s.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

RPushCmd::RPushCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool RPushCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPushCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPush(client->Key(), list_values, &reply_num);
  if (s_.ok()) {
    client->AppendInteger(reply_num);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "rpush cmd error");
  }
}

void RPushCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void RPushCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->RPushx(key, list_values);
  }
}

RPushxCmd::RPushxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool RPushxCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPushxCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPushx(client->Key(), list_values, &reply_num);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void RPushxCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void RPushxCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->RPushx(key, list_values);
  }
}

LPopCmd::LPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPop(client->Key(), 1, &elements);
  if (s_.ok()) {
    client->AppendString(elements[0]);
  } else if (s_.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void LPopCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LPopCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::string value;
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LPop(key, &value);
  }
}

RPopCmd::RPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool RPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPop(client->Key(), 1, &elements);
  if (s_.ok()) {
    client->AppendString(elements[0]);
  } else if (s_.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "rpop cmd error");
  }
}

void RPopCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void RPopCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::string value;
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->RPop(key, &value);
  }
}

LRangeCmd::LRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategoryList) {}

bool LRangeCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &start_index_) == 0 || pstd::String2int(client->argv_[3], &end_index_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LRangeCmd::DoCmd(PClient* client) {
  std::vector<std::string> ret;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRange(client->Key(), start_index_, end_index_, &ret);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "lrange cmd error");
    }
    return;
  }
  client->AppendStringVector(ret);
}

void LRangeCmd::ReadCache(PClient* client) {
  std::vector<std::string> values;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LRange(key, start_index_, end_index_, &values);
  if (s.ok()) {
    client->AppendArrayLen(values.size());
    for (const auto& value : values) {
      client->AppendString(value);
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void LRangeCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void LRangeCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_LIST, key, client);
  }
}

LRemCmd::LRemCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LRemCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &freq_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LRemCmd::DoCmd(PClient* client) {
  uint64_t reply_num = 0;
  s_ =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRem(client->Key(), freq_, client->argv_[3], &reply_num);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "lrem cmd error");
  }
}

void LRemCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LRemCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LRem(key, freq_, client->argv_[3]);
  }
}

LTrimCmd::LTrimCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LTrimCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &start_index_) == 0 || pstd::String2int(client->argv_[3], &end_index_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LTrimCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LTrim(client->Key(), start_index_, end_index_);
  if (s_.ok() || s_.IsNotFound()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "ltrim cmd error");
  }
}

void LTrimCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LTrimCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LTrim(key, start_index_, end_index_);
  }
}

LSetCmd::LSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LSetCmd::DoInitial(PClient* client) {
  // isValidNumber ensures that the string is in decimal format,
  // while strtol ensures that the string is within the range of long type
  const std::string index_str = client->argv_[2];
  if (!pstd::IsValidNumber(index_str)) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  if (1 != pstd::String2int(index_str, &index_)) {
    client->SetRes(CmdRes::kErrOther, "lset cmd error");  // this will not happend in normal case
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LSetCmd::DoCmd(PClient* client) {
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LSet(client->Key(), index_, client->argv_[3]);
  if (s_.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s_.IsNotFound()) {
    client->SetRes(CmdRes::kNotFound);
  } else if (s_.IsCorruption()) {
    client->SetRes(CmdRes::kOutOfRange);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "lset cmd error");  // just a safeguard
  }
}

void LSetCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LSetCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LSet(key, index_, client->argv_[3]);
  }
}

LInsertCmd::LInsertCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryList) {}

bool LInsertCmd::DoInitial(PClient* client) {
  if (!pstd::StringEqualCaseInsensitive(client->argv_[2], "BEFORE") &&
      !pstd::StringEqualCaseInsensitive(client->argv_[2], "AFTER")) {
    return false;
  }
  before_or_after_ = storage::Before;
  if (pstd::StringEqualCaseInsensitive(client->argv_[2], "AFTER")) {
    before_or_after_ = storage::After;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LInsertCmd::DoCmd(PClient* client) {
  int64_t ret = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->LInsert(client->Key(), before_or_after_, client->argv_[3], client->argv_[4], &ret);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "linsert cmd error");
    }
    return;
  }
  client->AppendInteger(ret);
}

void LInsertCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void LInsertCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())
        ->GetCache()
        ->LInsert(key, before_or_after_, client->argv_[3], client->argv_[4]);
  }
}

LIndexCmd::LIndexCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategoryList) {}

bool LIndexCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &index_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LIndexCmd::DoCmd(PClient* client) {
  std::string value;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LIndex(client->Key(), index_, &value);
  if (s_.ok()) {
    client->AppendString(value);
  } else if (s_.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void LIndexCmd::ReadCache(PClient* client) {
  std::string value;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LIndex(key, index_, &value);
  if (s.ok()) {
    client->AppendString(value);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void LIndexCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_LIST, key, client);
  }
}

void LIndexCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

LLenCmd::LLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsList | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategoryList) {}

bool LLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LLenCmd::DoCmd(PClient* client) {
  uint64_t llen = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LLen(client->Key(), &llen);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(static_cast<int64_t>(llen));
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void LLenCmd::ReadCache(PClient* client) {
  uint64_t llen = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->LLen(key, &llen);
  if (s.ok()) {
    client->AppendInteger(llen);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void LLenCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void LLenCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_LIST, key, client);
  }
}
}  // namespace pikiwidb
