/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_list.h"
#include "pikiwidb.h"
#include "pstd_string.h"
#include "store.h"

namespace pikiwidb {
LPushCmd::LPushCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LPushCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPushCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPush(client->Key(), list_values, &reply_num);
  if (s.ok()) {
    client->AppendInteger(reply_num);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "lpush cmd error");
  }
  ServeAndUnblockConns(client);
}

LPushxCmd::LPushxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LPushxCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPushxCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPushx(client->Key(), list_values, &reply_num);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
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
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool RPushCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPushCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPush(client->Key(), list_values, &reply_num);
  if (s.ok()) {
    client->AppendInteger(reply_num);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "rpush cmd error");
  }
  ServeAndUnblockConns(client);
}

RPushxCmd::RPushxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool RPushxCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPushxCmd::DoCmd(PClient* client) {
  std::vector<std::string> list_values(client->argv_.begin() + 2, client->argv_.end());
  uint64_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPushx(client->Key(), list_values, &reply_num);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

BLPopCmd::BLPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool BLPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  int64_t timeout = 0;
  if (!pstd::String2int(client->argv_.back().data(), client->argv_.back().size(), &timeout)) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  constexpr int64_t seconds_of_ten_years = 10 * 365 * 24 * 3600;
  if (timeout < 0 || timeout > seconds_of_ten_years) {
    client->SetRes(CmdRes::kErrOther,
                   "timeout can't be a negative value and can't exceed the number of seconds in 10 years");
    return false;
  }

  if (timeout > 0) {
    auto now = std::chrono::system_clock::now();
    expire_time_ =
        std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count() + timeout * 1000;
  }
  return true;
}

void BLPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  std::vector<std::string> list_keys(client->argv_.begin() + 1, client->argv_.end() - 1);
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPop(client->Key(), 1, &elements);
  if (s.ok()) {
    client->AppendArrayLen(2);
    client->AppendString(client->Key());
    client->AppendString(elements[0]);
    return;
  } else if (s.IsNotFound()) {
    BlockThisClientToWaitLRPush(elements, expire_time_, client, BlockedConnNode::Type::BLPop);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return;
  }
}

BRPopCmd::BRPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool BRPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  int64_t timeout = 0;
  if (!pstd::String2int(client->argv_.back().data(), client->argv_.back().size(), &timeout)) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  constexpr int64_t seconds_of_ten_years = 10 * 365 * 24 * 3600;
  if (timeout < 0 || timeout > seconds_of_ten_years) {
    client->SetRes(CmdRes::kErrOther,
                   "timeout can't be a negative value and can't exceed the number of seconds in 10 years");
    return false;
  }

  if (timeout > 0) {
    auto now = std::chrono::system_clock::now();
    expire_time_ =
        std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count() + timeout * 1000;
  }
  return true;
}

void BRPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPop(client->Key(), 1, &elements);
  if (s.ok()) {
    client->AppendArrayLen(2);
    client->AppendString(client->Key());
    client->AppendString(elements[0]);
    return;
  } else if (s.IsNotFound()) {
    BlockThisClientToWaitLRPush(elements, expire_time_, client, BlockedConnNode::Type::BRPop);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return;
  }
}

LPopCmd::LPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LPop(client->Key(), 1, &elements);
  if (s.ok()) {
    client->AppendString(elements[0]);
  } else if (s.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

RPopCmd::RPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool RPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void RPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> elements;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPop(client->Key(), 1, &elements);
  if (s.ok()) {
    client->AppendString(elements[0]);
  } else if (s.IsNotFound()) {
    client->AppendStringLen(-1);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "rpop cmd error");
  }
}

LRangeCmd::LRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryList) {}

bool LRangeCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LRangeCmd::DoCmd(PClient* client) {
  std::vector<std::string> ret;
  int64_t start_index = 0;
  int64_t end_index = 0;
  if (pstd::String2int(client->argv_[2], &start_index) == 0 || pstd::String2int(client->argv_[3], &end_index) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRange(client->Key(), start_index, end_index, &ret);
  if (!s.ok() && !s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "lrange cmd error");
    }
    return;
  }
  client->AppendStringVector(ret);
}

LRemCmd::LRemCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LRemCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LRemCmd::DoCmd(PClient* client) {
  int64_t freq_ = 0;
  std::string count = client->argv_[2];
  if (pstd::String2int(count, &freq_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  uint64_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRem(client->Key(), freq_, client->argv_[3], &reply_num);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "lrem cmd error");
  }
}

LTrimCmd::LTrimCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LTrimCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LTrimCmd::DoCmd(PClient* client) {
  int64_t start_index = 0;
  int64_t end_index = 0;

  if (pstd::String2int(client->argv_[2], &start_index) == 0 || pstd::String2int(client->argv_[3], &end_index) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LTrim(client->Key(), start_index, end_index);
  if (s.ok() || s.IsNotFound()) {
    client->SetRes(CmdRes::kOK);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "ltrim cmd error");
  }
}

LSetCmd::LSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LSetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LSetCmd::DoCmd(PClient* client) {
  // isValidNumber ensures that the string is in decimal format,
  // while strtol ensures that the string is within the range of long type
  const std::string index_str = client->argv_[2];

  if (pstd::IsValidNumber(index_str)) {
    int64_t val = 0;
    if (1 != pstd::String2int(index_str, &val)) {
      client->SetRes(CmdRes::kErrOther, "lset cmd error");  // this will not happend in normal case
      return;
    }
    storage::Status s =
        PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LSet(client->Key(), val, client->argv_[3]);
    if (s.ok()) {
      client->SetRes(CmdRes::kOK);
    } else if (s.IsNotFound()) {
      client->SetRes(CmdRes::kNotFound);
    } else if (s.IsCorruption()) {
      client->SetRes(CmdRes::kOutOfRange);
    } else if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "lset cmd error");  // just a safeguard
    }
  } else {
    client->SetRes(CmdRes::kInvalidInt);
  }
}

LInsertCmd::LInsertCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool LInsertCmd::DoInitial(PClient* client) {
  if (!pstd::StringEqualCaseInsensitive(client->argv_[2], "BEFORE") &&
      !pstd::StringEqualCaseInsensitive(client->argv_[2], "AFTER")) {
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void LInsertCmd::DoCmd(PClient* client) {
  int64_t ret = 0;
  storage ::BeforeOrAfter before_or_after = storage::Before;
  if (pstd::StringEqualCaseInsensitive(client->argv_[2], "AFTER")) {
    before_or_after = storage::After;
  }
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->LInsert(client->Key(), before_or_after, client->argv_[3], client->argv_[4], &ret);
  if (!s.ok() && !s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "linsert cmd error");
    }
    return;
  }
  client->AppendInteger(ret);
}

LIndexCmd::LIndexCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryList) {}

bool LIndexCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LIndexCmd::DoCmd(PClient* client) {
  int64_t freq_ = 0;
  std::string count = client->argv_[2];
  if (pstd::String2int(count, &freq_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  std::string value;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LIndex(client->Key(), freq_, &value);
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

LLenCmd::LLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryList) {}

bool LLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void LLenCmd::DoCmd(PClient* client) {
  uint64_t llen = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LLen(client->Key(), &llen);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(static_cast<int64_t>(llen));
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}
}  // namespace pikiwidb
