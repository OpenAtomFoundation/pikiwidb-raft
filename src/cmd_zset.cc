// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Implemented a set of features related to sorted sets.
 */

#include "cmd_zset.h"

#include <memory>

#include "pstd/pstd_string.h"
#include "store.h"

namespace pikiwidb {

static void FitLimit(int64_t& count, int64_t& offset, const int64_t size) {
  count = count >= 0 ? count : size;
  offset = (offset >= 0 && offset < size) ? offset : size;
  count = (offset + count < size) ? count : size - offset;
}

int32_t DoScoreStrRange(std::string begin_score, std::string end_score, bool* left_close, bool* right_close,
                        double* min_score, double* max_score) {
  if (!begin_score.empty() && begin_score.at(0) == '(') {
    *left_close = false;
    begin_score.erase(begin_score.begin());
  }
  if (begin_score == "-inf") {
    *min_score = storage::ZSET_SCORE_MIN;
  } else if (begin_score == "inf" || begin_score == "+inf") {
    *min_score = storage::ZSET_SCORE_MAX;
  } else if (pstd::String2d(begin_score.data(), begin_score.size(), min_score) == 0) {
    return -1;
  }

  if (!end_score.empty() && end_score.at(0) == '(') {
    *right_close = false;
    end_score.erase(end_score.begin());
  }
  if (end_score == "+inf" || end_score == "inf") {
    *max_score = storage::ZSET_SCORE_MAX;
  } else if (end_score == "-inf") {
    *max_score = storage::ZSET_SCORE_MIN;
  } else if (pstd::String2d(end_score.data(), end_score.size(), max_score) == 0) {
    return -1;
  }
  return 0;
}

static int32_t DoMemberRange(const std::string& raw_min_member, const std::string& raw_max_member, bool* left_close,
                             bool* right_close, std::string* min_member, std::string* max_member) {
  if (raw_min_member == "-") {
    *min_member = "-";
  } else if (raw_min_member == "+") {
    *min_member = "+";
  } else {
    if (!raw_min_member.empty() && raw_min_member.at(0) == '(') {
      *left_close = false;
    } else if (!raw_min_member.empty() && raw_min_member.at(0) == '[') {
      *left_close = true;
    } else {
      return -1;
    }
    min_member->assign(raw_min_member.begin() + 1, raw_min_member.end());
  }

  if (raw_max_member == "+") {
    *max_member = "+";
  } else if (raw_max_member == "-") {
    *max_member = "-";
  } else {
    if (!raw_max_member.empty() && raw_max_member.at(0) == '(') {
      *right_close = false;
    } else if (!raw_max_member.empty() && raw_max_member.at(0) == '[') {
      *right_close = true;
    } else {
      return -1;
    }
    max_member->assign(raw_max_member.begin() + 1, raw_max_member.end());
  }
  return 0;
}

ZAddCmd::ZAddCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZAddCmd::DoInitial(PClient* client) {
  size_t argc = client->argv_.size();
  if (argc % 2 == 1) {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }

  score_members_.clear();
  double score = 0.0;
  size_t index = 2;
  for (; index < argc; index += 2) {
    if (pstd::String2d(client->argv_[index].data(), client->argv_[index].size(), &score) == 0) {
      client->SetRes(CmdRes::kInvalidFloat);
      return false;
    }
    score_members_.push_back({score, client->argv_[index + 1]});
  }

  client->SetKey(client->argv_[1]);
  return true;
}

void ZAddCmd::DoCmd(PClient* client) {
  int32_t count = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZAdd(client->Key(), score_members_, &count);
  if (s_.ok()) {
    client->AppendInteger(count);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZAddCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZAddCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZAddIfKeyExistInCache(key, score_members_, client);
  }
}

ZPopMinCmd::ZPopMinCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZPopMinCmd::DoInitial(PClient* client) {
  if (client->argv_.size() > 3) {
    client->SetRes(CmdRes::kWrongNum, client->CmdName());
    return false;
  }

  client->SetKey(client->argv_[1]);
  return true;
}

void ZPopMinCmd::DoCmd(PClient* client) {
  int32_t count = 1;
  if (client->argv_.size() == 3) {
    if (pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &count) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }
  }

  std::vector<storage::ScoreMember> score_members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZPopMin(client->Key(), count, &score_members);
  if (s_.ok()) {
    char buf[32];
    int64_t len = 0;
    client->AppendArrayLen(static_cast<int64_t>(score_members.size()) * 2);
    for (auto& score_member : score_members) {
      client->AppendStringLenUint64(score_member.member.size());
      client->AppendContent(score_member.member);
      len = pstd::D2string(buf, sizeof(buf), score_member.score);
      client->AppendStringLen(len);
      client->AppendContent(buf);
    }
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZPopMinCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZPopMinCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v;
    v.emplace_back(client->Key());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
  }
}

ZPopMaxCmd::ZPopMaxCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZPopMaxCmd::DoInitial(PClient* client) {
  if (client->argv_.size() > 3) {
    client->SetRes(CmdRes::kWrongNum, client->CmdName());
    return false;
  }

  client->SetKey(client->argv_[1]);
  return true;
}

void ZPopMaxCmd::DoCmd(PClient* client) {
  int32_t count = 1;
  if (client->argv_.size() == 3) {
    if (pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &count) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }
  }

  std::vector<storage::ScoreMember> score_members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZPopMax(client->Key(), count, &score_members);
  if (s_.ok()) {
    char buf[32];
    int64_t len = 0;
    client->AppendArrayLen(static_cast<int64_t>(score_members.size()) * 2);
    for (auto& score_member : score_members) {
      client->AppendStringLenUint64(score_member.member.size());
      client->AppendContent(score_member.member);
      len = pstd::D2string(buf, sizeof(buf), score_member.score);
      client->AppendStringLen(len);
      client->AppendContent(buf);
    }
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZPopMaxCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZPopMaxCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v;
    v.emplace_back(client->Key());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
  }
}

ZsetUIstoreParentCmd::ZsetUIstoreParentCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

// ZINTERSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE <SUM | MIN | MAX>]
// ZUNIONSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE <SUM | MIN | MAX>]
bool ZsetUIstoreParentCmd::DoInitial(PClient* client) {
  auto argv_ = client->argv_;
  dest_key_ = argv_[1];
  if (pstd::String2int(argv_[2].data(), argv_[2].size(), &num_keys_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  if (num_keys_ < 1) {
    client->SetRes(CmdRes::kErrOther, "at least 1 input key is needed for ZUNIONSTORE/ZINTERSTORE");
    return false;
  }
  auto argc = argv_.size();
  if (argc < num_keys_ + 3) {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }
  keys_.assign(argv_.begin() + 3, argv_.begin() + 3 + num_keys_);
  weights_.assign(num_keys_, 1);
  auto index = num_keys_ + 3;
  while (index < argc) {
    if (strcasecmp(argv_[index].data(), "weights") == 0) {
      index++;
      if (argc < index + num_keys_) {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      double weight;
      auto base = index;
      for (; index < base + num_keys_; index++) {
        if (pstd::String2d(argv_[index].data(), argv_[index].size(), &weight) == 0) {
          client->SetRes(CmdRes::kErrOther, "weight value is not a float");
          return false;
        }
        weights_[index - base] = weight;
      }
    } else if (strcasecmp(argv_[index].data(), "aggregate") == 0) {
      index++;
      if (argc < index + 1) {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      if (strcasecmp(argv_[index].data(), "sum") == 0) {
        aggregate_ = storage::SUM;
      } else if (strcasecmp(argv_[index].data(), "min") == 0) {
        aggregate_ = storage::MIN;
      } else if (strcasecmp(argv_[index].data(), "max") == 0) {
        aggregate_ = storage::MAX;
      } else {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      index++;
    } else {
      client->SetRes(CmdRes::kSyntaxErr);
      return false;
    }
  }
  return true;
}

ZInterstoreCmd::ZInterstoreCmd(const std::string& name, int16_t arity) : ZsetUIstoreParentCmd(name, arity) {}

bool ZInterstoreCmd::DoInitial(PClient* client) { return ZsetUIstoreParentCmd::DoInitial(client); }

void ZInterstoreCmd::DoCmd(PClient* client) {
  int32_t count = 0;
  std::vector<storage::ScoreMember> value_to_dest_;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZInterstore(dest_key_, keys_, weights_, aggregate_, value_to_dest_, &count);
  if (s_.ok()) {
    client->AppendInteger(count);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZInterstoreCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZInterstoreCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v;
    v.emplace_back(dest_key_);
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
  }
}

ZUnionstoreCmd::ZUnionstoreCmd(const std::string& name, int16_t arity) : ZsetUIstoreParentCmd(name, arity) {}

bool ZUnionstoreCmd::DoInitial(PClient* client) { return ZsetUIstoreParentCmd::DoInitial(client); }

void ZUnionstoreCmd::DoCmd(PClient* client) {
  int32_t count = 0;
  std::map<std::string, double> value_to_dest;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZUnionstore(dest_key_, keys_, weights_, aggregate_, value_to_dest, &count);
  if (s_.ok()) {
    client->AppendInteger(count);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZUnionstoreCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZUnionstoreCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    std::vector<std::string> v;
    v.emplace_back(dest_key_);
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->Del(v);
  }
}

ZRevrangeCmd::ZRevrangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySortedSet) {}

bool ZRevrangeCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRevrangeCmd::DoCmd(PClient* client) {
  std::string key;
  int64_t start = 0;
  int64_t stop = -1;
  bool is_ws = false;
  if (client->argv_.size() == 5 && (strcasecmp(client->argv_[4].data(), "withscores") == 0)) {
    is_ws = true;
  } else if (client->argv_.size() != 4) {
    client->SetRes(CmdRes::kSyntaxErr);
    return;
  }
  if (pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &start) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  if (pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &stop) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }
  std::vector<storage::ScoreMember> score_members;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())
          ->GetStorage()
          ->ZRevrange(client->Key(), static_cast<int32_t>(start), static_cast<int32_t>(stop), &score_members);
  if (s.ok() || s.IsNotFound()) {
    if (is_ws) {
      char buf[32];
      int64_t len;
      client->AppendArrayLenUint64(score_members.size() * 2);
      for (const auto& sm : score_members) {
        client->AppendStringLenUint64(sm.member.size());
        client->AppendContent(sm.member);
        len = pstd::D2string(buf, sizeof(buf), sm.score);
        client->AppendStringLen(len);
        client->AppendContent(buf);
      }
    } else {
      client->AppendArrayLenUint64(score_members.size());
      for (const auto& sm : score_members) {
        client->AppendStringLenUint64(sm.member.size());
        client->AppendContent(sm.member);
      }
    }
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

ZRangebyscoreCmd::ZRangebyscoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategorySortedSet) {}

bool ZRangebyscoreCmd::DoInitial(PClient* client) {
  int32_t ret =
      DoScoreStrRange(client->argv_[2], client->argv_[3], &left_close_, &right_close_, &min_score_, &max_score_);
  if (ret == -1) {
    client->SetRes(CmdRes::kErrOther, "min or max is not a float");
    return false;
  }
  size_t argc = client->argv_.size();
  if (argc >= 5) {
    size_t index = 4;
    while (index < argc) {
      if (strcasecmp(client->argv_[index].data(), "withscores") == 0) {
        with_scores_ = true;
      } else if (strcasecmp(client->argv_[index].data(), "limit") == 0) {
        if (index + 3 > argc) {
          client->SetRes(CmdRes::kSyntaxErr);
          return false;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &offset_) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return false;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &count_) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return false;
        }
      } else {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      index++;
    }
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRangebyscoreCmd::DoCmd(PClient* client) {
  if (min_score_ == storage::ZSET_SCORE_MAX || max_score_ == storage::ZSET_SCORE_MIN) {
    client->AppendContent("*0");
    return;
  }
  std::vector<storage::ScoreMember> score_members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZRangebyscore(client->Key(), min_score_, max_score_, left_close_, right_close_, &score_members);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }
  FitLimit(count_, offset_, static_cast<int64_t>(score_members.size()));
  size_t start = offset_;
  size_t end = offset_ + count_;
  if (with_scores_) {
    char buf[32];
    int64_t len = 0;
    client->AppendArrayLen(count_ * 2);
    for (; start < end; start++) {
      client->AppendStringLenUint64(score_members[start].member.size());
      client->AppendContent(score_members[start].member);
      len = pstd::D2string(buf, sizeof(buf), score_members[start].score);
      client->AppendStringLen(len);
      client->AppendContent(buf);
    }
  } else {
    client->AppendArrayLen(count_);
    for (; start < end; start++) {
      client->AppendStringLenUint64(score_members[start].member.size());
      client->AppendContent(score_members[start].member);
    }
  }
}

void ZRangebyscoreCmd::ReadCache(PClient* client) {
  if (min_score_ == storage::ZSET_SCORE_MAX || max_score_ == storage::ZSET_SCORE_MIN) {
    client->AppendContent("*0");
    return;
  }

  std::vector<storage::ScoreMember> score_members;
  auto min = std::to_string(min_score_);
  auto max = std::to_string(max_score_);
  auto key = client->Key();
  size_t argc = client->argv_.size();
  if (argc < 5) {
    // to escape occasional bug
    ResetCount();
  }
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRangebyscore(key, min, max, &score_members, this);
  if (s.ok()) {
    auto sm_count = score_members.size();
    if (with_scores_) {
      char buf[32];
      int64_t len;
      client->AppendArrayLen(sm_count * 2);
      for (auto& item : score_members) {
        client->AppendStringLen(item.member.size());
        client->AppendContent(item.member);
        len = pstd::D2string(buf, sizeof(buf), item.score);
        client->AppendStringLen(len);
        client->AppendContent(buf);
      }
    } else {
      client->AppendArrayLen(sm_count);
      for (auto& item : score_members) {
        client->AppendStringLen(item.member.size());
        client->AppendContent(item.member);
      }
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRangebyscoreCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRangebyscoreCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZRemrangebyrankCmd::ZRemrangebyrankCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategoryString) {}

bool ZRemrangebyrankCmd::DoInitial(PClient* client) {
  if (pstd::String2int(client->argv_[2], &start_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  if (pstd::String2int(client->argv_[3], &end_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRemrangebyrankCmd::DoCmd(PClient* client) {
  int32_t ret = 0;

  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRemrangebyrank(client->Key(), start_, end_, &ret);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(ret);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZRemrangebyrankCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZRemrangebyrankCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRemrangebyrank(key, start_, end_);
  }
}

ZRevrangebyscoreCmd::ZRevrangebyscoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZRevrangebyscoreCmd::DoInitial(PClient* client) {
  int32_t ret =
      DoScoreStrRange(client->argv_[3], client->argv_[2], &left_close_, &right_close_, &min_score_, &max_score_);
  if (ret == -1) {
    client->SetRes(CmdRes::kErrOther, "min or max is not a float");
    return false;
  }

  size_t argc = client->argv_.size();
  if (argc >= 5) {
    size_t index = 4;
    while (index < argc) {
      if (strcasecmp(client->argv_[index].data(), "withscores") == 0) {
        with_scores_ = true;
      } else if (strcasecmp(client->argv_[index].data(), "limit") == 0) {
        if (index + 3 > argc) {
          client->SetRes(CmdRes::kSyntaxErr);
          return false;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &offset_) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return false;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &count_) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return false;
        }
      } else {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      index++;
    }
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRevrangebyscoreCmd::DoCmd(PClient* client) {
  if (min_score_ == storage::ZSET_SCORE_MAX || max_score_ == storage::ZSET_SCORE_MIN) {
    client->AppendContent("*0");
    return;
  }
  std::vector<storage::ScoreMember> score_members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZRevrangebyscore(client->Key(), min_score_, max_score_, left_close_, right_close_, &score_members);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }
  FitLimit(count_, offset_, static_cast<int64_t>(score_members.size()));
  size_t start = offset_;
  size_t end = offset_ + count_;
  if (with_scores_) {
    char buf[32];
    int64_t len = 0;
    client->AppendArrayLen(count_ * 2);
    for (; start < end; start++) {
      client->AppendStringLenUint64(score_members[start].member.size());
      client->AppendContent(score_members[start].member);
      len = pstd::D2string(buf, sizeof(buf), score_members[start].score);
      client->AppendStringLen(len);
      client->AppendContent(buf);
    }
  } else {
    client->AppendArrayLen(count_);
    for (; start < end; start++) {
      client->AppendStringLenUint64(score_members[start].member.size());
      client->AppendContent(score_members[start].member);
    }
  }
}

void ZRevrangebyscoreCmd::ReadCache(PClient* client) {
  if (min_score_ == storage::ZSET_SCORE_MAX || max_score_ == storage::ZSET_SCORE_MIN) {
    client->AppendContent("*0");
    return;
  }
  size_t argc = client->argv_.size();
  if (argc < 5) {
    // to escape occasional bug
    ResetCount();
  }

  std::vector<storage::ScoreMember> score_members;
  auto min = std::to_string(min_score_);
  auto max = std::to_string(max_score_);
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRevrangebyscore(key, min, max, &score_members, this);
  if (s.ok()) {
    auto sm_count = score_members.size();
    if (with_scores_) {
      char buf[32];
      int64_t len;
      client->AppendArrayLen(sm_count * 2);
      for (auto& item : score_members) {
        client->AppendStringLen(item.member.size());
        client->AppendContent(item.member);
        len = pstd::D2string(buf, sizeof(buf), item.score);
        client->AppendStringLen(len);
        client->AppendContent(buf);
      }
    } else {
      client->AppendArrayLen(sm_count);
      for (auto& item : score_members) {
        client->AppendStringLen(item.member.size());
        client->AppendContent(item.member);
      }
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRevrangebyscoreCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRevrangebyscoreCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZCardCmd::ZCardCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategorySortedSet) {}

bool ZCardCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZCardCmd::DoCmd(PClient* client) {
  int32_t reply_Num = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZCard(client->Key(), &reply_Num);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_Num);
  } else {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "ZCard cmd error");
    }
    return;
  }
}

void ZCardCmd::ReadCache(PClient* client) {
  auto key = client->Key();
  uint64_t len = 0;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZCard(key, &len);
  if (s.ok()) {
    client->AppendInteger(len);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZCardCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZCardCmd::DoUpdateCache(PClient* client) { return; }

ZRangeCmd::ZRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySortedSet) {}

bool ZRangeCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRangeCmd::DoCmd(PClient* client) {
  double start = 0;
  double stop = 0;
  int64_t count = -1;
  int64_t offset = 0;
  bool with_scores = false;
  bool by_score = false;
  bool by_lex = false;
  bool left_close = false;
  bool right_close = false;
  bool is_rev = false;
  size_t argc = client->argv_.size();
  if (argc >= 5) {
    size_t index = 4;
    while (index < argc) {
      if (strcasecmp(client->argv_[index].data(), "byscore") == 0) {
        by_score = true;
      } else if (strcasecmp(client->argv_[index].data(), "bylex") == 0) {
        by_lex = true;
      } else if (strcasecmp(client->argv_[index].data(), "rev") == 0) {
        is_rev = true;
      } else if (strcasecmp(client->argv_[index].data(), "withscores") == 0) {
        with_scores = true;
      } else if (strcasecmp(client->argv_[index].data(), "limit") == 0) {
        if (index + 3 > argc) {
          client->SetRes(CmdRes::kSyntaxErr);
          return;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &offset) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return;
        }
        index++;
        if (pstd::String2int(client->argv_[index].data(), client->argv_[index].size(), &count) == 0) {
          client->SetRes(CmdRes::kInvalidInt);
          return;
        }
      } else {
        client->SetRes(CmdRes::kSyntaxErr);
        return;
      }
      index++;
    }
  }
  if (by_score && by_lex) {
    client->SetRes(CmdRes::kSyntaxErr);
    return;
  }

  int32_t ret = 0;
  std::string lex_min;
  std::string lex_max;
  if (by_lex) {
    ret = DoMemberRange(client->argv_[2], client->argv_[3], &left_close, &right_close, &lex_min, &lex_max);
    if (ret == -1) {
      client->SetRes(CmdRes::kErrOther, "min or max not valid string range item");
      return;
    }
  } else {
    ret = DoScoreStrRange(client->argv_[2], client->argv_[3], &left_close, &right_close, &start, &stop);
    if (ret == -1) {
      client->SetRes(CmdRes::kErrOther, "start or stop is not a float");
      return;
    }
  }

  std::vector<storage::ScoreMember> score_members;
  std::vector<std::string> lex_members;
  storage::Status s;
  if (!is_rev) {
    if (by_score) {
      s = PSTORE.GetBackend(client->GetCurrentDB())
              ->GetStorage()
              ->ZRangebyscore(client->Key(), start, stop, left_close, right_close, &score_members);
    } else if (by_lex) {
      s = PSTORE.GetBackend(client->GetCurrentDB())
              ->GetStorage()
              ->ZRangebylex(client->Key(), lex_min, lex_max, left_close, right_close, &lex_members);
    } else {
      s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRange(client->Key(), start, stop, &score_members);
    }
  } else {
    if (by_score) {
      s = PSTORE.GetBackend(client->GetCurrentDB())
              ->GetStorage()
              ->ZRevrangebyscore(client->Key(), start, stop, left_close, right_close, &score_members);
    } else if (by_lex) {
      s = PSTORE.GetBackend(client->GetCurrentDB())
              ->GetStorage()
              ->ZRangebylex(client->Key(), lex_min, lex_max, left_close, right_close, &lex_members);
    } else {
      s = PSTORE.GetBackend(client->GetCurrentDB())
              ->GetStorage()
              ->ZRevrange(client->Key(), start, stop, &score_members);
    }
  }
  if (!s.ok() && !s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
    return;
  }
  FitLimit(count, offset, static_cast<int64_t>(score_members.size()));
  size_t m_start = offset;
  size_t m_end = offset + count;
  if (by_lex) {
    if (with_scores) {
      client->SetRes(CmdRes::kSyntaxErr, "by lex not support with scores");
    } else {
      client->AppendArrayLen(count);
      for (; m_start < m_end; m_start++) {
        client->AppendContent(lex_members[m_start]);
      }
    }
  } else {
    if (with_scores) {
      char buf[32];
      int64_t len = 0;
      client->AppendArrayLen(count * 2);
      for (; m_start < m_end; m_start++) {
        client->AppendStringLenUint64(score_members[m_start].member.size());
        client->AppendContent(score_members[m_start].member);
        len = pstd::D2string(buf, sizeof(buf), score_members[m_start].score);
        client->AppendStringLen(len);
        client->AppendContent(buf);
      }
    } else {
      client->AppendArrayLen(count);
      for (; m_start < m_end; m_start++) {
        client->AppendStringLenUint64(score_members[m_start].member.size());
        client->AppendContent(score_members[m_start].member);
      }
    }
  }
}

ZScoreCmd::ZScoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsReadCache,
              kAclCategoryRead | kAclCategoryString) {}

bool ZScoreCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZScoreCmd::DoCmd(PClient* client) {
  double score = 0;

  storage::Status s;
  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZScore(client->Key(), client->argv_[2], &score);
  if (s.ok() || s.IsNotFound()) {
    char buf[32];
    int64_t len = pstd::D2string(buf, sizeof(buf), score);
    client->AppendStringLen(len);
    client->AppendContent(buf);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZScoreCmd::ReadCache(PClient* client) {
  double score = 0.0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZScore(key, client->argv_[2], &score);
  if (s.ok()) {
    char buf[32];
    int64_t len = pstd::D2string(buf, sizeof(buf), score);
    client->AppendStringLen(len);
    client->AppendContent(buf);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZScoreCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZScoreCmd::DoUpdateCache(PClient* client) { return; }

ZRangebylexCmd::ZRangebylexCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZRangebylexCmd::DoInitial(PClient* client) {
  size_t argc = client->argv_.size();
  if (argc == 7 && strcasecmp(client->argv_[4].data(), "limit") == 0) {
    if (pstd::String2int(client->argv_[5].data(), client->argv_[5].size(), &offset_) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return false;
    }
    if (pstd::String2int(client->argv_[6].data(), client->argv_[6].size(), &count_) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return false;
    }
  } else if (argc == 4) {
  } else {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }

  int32_t ret =
      DoMemberRange(client->argv_[2], client->argv_[3], &left_close_, &right_close_, &min_member_, &max_member_);
  if (ret == -1) {
    client->SetRes(CmdRes::kErrOther, "min or max not valid string range item");
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRangebylexCmd::DoCmd(PClient* client) {
  if (strcasecmp(client->argv_[2].data(), "+") == 0 || strcasecmp(client->argv_[3].data(), "-") == 0) {
    client->AppendContent("*0");
  }

  std::vector<std::string> members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZRangebylex(client->Key(), min_member_, max_member_, left_close_, right_close_, &members);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }

  FitLimit(count_, offset_, static_cast<int64_t>(members.size()));
  size_t index = offset_;
  size_t end = offset_ + count_;

  client->AppendArrayLen(static_cast<int64_t>(members.size()));
  for (; index < end; index++) {
    client->AppendStringLenUint64(members[index].size());
    client->AppendContent(members[index]);
  }
}

void ZRangebylexCmd::ReadCache(PClient* client) {
  if (min_member_ == "+" || max_member_ == "-") {
    client->AppendContent("*0");
    return;
  }
  std::vector<std::string> members;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())
               ->GetCache()
               ->ZRangebylex(key, client->argv_[2], client->argv_[3], &members);
  if (s.ok()) {
    FitLimit(count_, offset_, members.size());

    client->AppendArrayLen(count_);
    size_t index = offset_;
    size_t end = offset_ + count_;
    for (; index < end; index++) {
      client->AppendStringLen(members[index].size());
      client->AppendContent(members[index]);
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRangebylexCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRangebylexCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZRevrangebylexCmd::ZRevrangebylexCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache | kCmdFlagsReadCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZRevrangebylexCmd::DoInitial(PClient* client) {
  size_t argc = client->argv_.size();

  if (argc == 7 && strcasecmp(client->argv_[4].data(), "limit") == 0) {
    if (pstd::String2int(client->argv_[5].data(), client->argv_[5].size(), &offset_) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return false;
    }
    if (pstd::String2int(client->argv_[6].data(), client->argv_[6].size(), &count_) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return false;
    }
  } else if (argc == 4) {
  } else {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }

  int32_t ret =
      DoMemberRange(client->argv_[2], client->argv_[3], &left_close_, &right_close_, &min_member_, &max_member_);
  if (ret == -1) {
    client->SetRes(CmdRes::kErrOther, "min or max not valid string range item");
    return false;
  }

  client->SetKey(client->argv_[1]);
  return true;
}

void ZRevrangebylexCmd::DoCmd(PClient* client) {
  if (strcasecmp(client->argv_[2].data(), "+") == 0 || strcasecmp(client->argv_[3].data(), "-") == 0) {
    client->AppendContent("*0");
  }

  std::vector<std::string> members;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZRangebylex(client->Key(), min_member_, max_member_, left_close_, right_close_, &members);
  if (!s_.ok() && !s_.IsNotFound()) {
    if (s_.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s_.ToString());
    }
    return;
  }

  FitLimit(count_, offset_, static_cast<int64_t>(members.size()));
  size_t index = offset_ + count_ - 1;
  size_t start = offset_;
  client->AppendArrayLen(static_cast<int64_t>(members.size()));
  for (; index >= start; index--) {
    client->AppendStringLenUint64(members[index].size());
    client->AppendContent(members[index]);
  }
}

void ZRevrangebylexCmd::ReadCache(PClient* client) {
  if (min_member_ == "+" || max_member_ == "-") {
    client->AppendContent("*0");
    return;
  }
  std::vector<std::string> members;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())
               ->GetCache()
               ->ZRevrangebylex(key, client->argv_[2], client->argv_[3], &members);
  if (s.ok()) {
    auto size = count_ < members.size() ? count_ : members.size();
    client->AppendArrayLen(static_cast<int64_t>(size));
    for (int i = 0; i < size; ++i) {
      client->AppendString(members[i]);
    }
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRevrangebylexCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRevrangebylexCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZRankCmd::ZRankCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategorySortedSet) {}

bool ZRankCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRankCmd::DoCmd(PClient* client) {
  int32_t rank = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRank(client->Key(), client->argv_[2], &rank);
  if (s_.ok()) {
    client->AppendInteger(rank);
  } else if (s_.IsNotFound()) {
    client->AppendContent("$-1");
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZRankCmd::ReadCache(PClient* client) {
  int64_t rank = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRank(key, client->argv_[2], &rank);
  if (s.ok()) {
    client->AppendInteger(rank);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRankCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRankCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZRevrankCmd::ZRevrankCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity,
              kCmdFlagsReadonly | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsReadCache | kCmdFlagsUpdateCache,
              kAclCategoryRead | kAclCategorySortedSet) {}

bool ZRevrankCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRevrankCmd::DoCmd(PClient* client) {
  int32_t revrank = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRevrank(client->Key(), client->argv_[2], &revrank);
  if (s_.ok()) {
    client->AppendInteger(revrank);
  } else if (s_.IsNotFound()) {
    client->AppendContent("$-1");
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZRevrankCmd::ReadCache(PClient* client) {
  int64_t revrank = 0;
  auto key = client->Key();
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRevrank(key, client->argv_[2], &revrank);
  if (s.ok()) {
    client->AppendInteger(revrank);
  } else if (s.IsNotFound()) {
    client->SetRes(CmdRes::kCacheMiss);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

void ZRevrankCmd::DoThroughDB(PClient* client) {
  client->Clear();
  DoCmd(client);
}

void ZRevrankCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
  }
}

ZRemCmd::ZRemCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZRemCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRemCmd::DoCmd(PClient* client) {
  auto iter = client->argv_.begin() + 2;
  std::vector<std::string> members(iter, client->argv_.end());
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRem(client->Key(), members, &deleted_);
  if (s_.ok() || s_.IsNotFound()) {
    client->AppendInteger(deleted_);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZRemCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZRemCmd::DoUpdateCache(PClient* client) {
  if (s_.ok() && deleted_ > 0) {
    auto key = client->Key();
    std::vector<std::string> members(client->argv_.begin() + 2, client->argv_.end());
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRem(key, members);
  }
}

ZIncrbyCmd::ZIncrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZIncrbyCmd::DoInitial(PClient* client) {
  if (pstd::String2d(client->argv_[2].data(), client->argv_[2].size(), &by_) == 0) {
    client->SetRes(CmdRes::kInvalidFloat);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void ZIncrbyCmd::DoCmd(PClient* client) {
  std::string member = client->argv_[3];
  s_ = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZIncrby(client->Key(), member, by_, &score_);
  if (s_.ok()) {
    char buf[32];
    int64_t len = pstd::D2string(buf, sizeof(buf), score_);
    client->AppendStringLen(len);
    client->AppendContent(buf);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZIncrbyCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZIncrbyCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    std::string member = client->argv_[3];
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZIncrbyIfKeyExist(key, member, by_, score_, client);
  }
}

ZRemrangebyscoreCmd::ZRemrangebyscoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite | kCmdFlagsZset | kCmdFlagsDoThroughDB | kCmdFlagsUpdateCache,
              kAclCategoryWrite | kAclCategorySortedSet) {}

bool ZRemrangebyscoreCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void ZRemrangebyscoreCmd::DoCmd(PClient* client) {
  double min_score = 0;
  double max_score = 0;
  bool left_close = true;
  bool right_close = true;
  int32_t ret = DoScoreStrRange(client->argv_[2], client->argv_[3], &left_close, &right_close, &min_score, &max_score);
  if (ret == -1) {
    client->SetRes(CmdRes::kErrOther, "min or max is not a float");
    return;
  }

  int32_t s_ret = 0;
  s_ = PSTORE.GetBackend(client->GetCurrentDB())
           ->GetStorage()
           ->ZRemrangebyscore(client->Key(), min_score, max_score, left_close, right_close, &s_ret);
  if (s_.ok()) {
    client->AppendInteger(s_ret);
  } else if (s_.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s_.ToString());
  }
}

void ZRemrangebyscoreCmd::DoThroughDB(PClient* client) { DoCmd(client); }

void ZRemrangebyscoreCmd::DoUpdateCache(PClient* client) {
  if (s_.ok()) {
    auto key = client->Key();
    PSTORE.GetBackend(client->GetCurrentDB())->GetCache()->ZRemrangebyscore(key, client->argv_[2], client->argv_[3]);
  }
}

}  // namespace pikiwidb
