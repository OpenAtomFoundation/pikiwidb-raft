/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_set.h"
#include <memory>
#include <utility>
#include "pstd/pstd_string.h"
#include "store.h"

namespace pikiwidb {

SIsMemberCmd::SIsMemberCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SIsMemberCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}
void SIsMemberCmd::DoCmd(PClient* client) {
  int32_t reply_Num = 0;  // only change to 1 if ismember . key not exist it is 0
  auto s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SIsmember(client->Key(), client->argv_[2], &reply_Num);
  if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
    return;
  }
  client->AppendInteger(reply_Num);
}

SAddCmd::SAddCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SAddCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}
// Integer reply: the number of elements that were added to the set,
// not including all the elements already present in the set.
void SAddCmd::DoCmd(PClient* client) {
  const std::vector<std::string> members(client->argv_.begin() + 2, client->argv_.end());
  int32_t ret = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SAdd(client->Key(), members, &ret);
  if (s.ok()) {
    client->AppendInteger(ret);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "sadd cmd error");
  }
}

SUnionStoreCmd::SUnionStoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SUnionStoreCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin() + 1, client->argv_.end());
  client->SetKey(keys);
  return true;
}

void SUnionStoreCmd::DoCmd(PClient* client) {
  std::vector<std::string> keys(client->Keys().begin() + 1, client->Keys().end());
  std::vector<std::string> value_to_dest;
  int32_t ret = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->SUnionstore(client->Keys().at(0), keys, value_to_dest, &ret);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
      return;
    }
    client->SetRes(CmdRes::kSyntaxErr, "sunionstore cmd error");
  }
  client->AppendInteger(ret);
}
SInterCmd::SInterCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SInterCmd::DoInitial(PClient* client) {
  std::vector keys(client->argv_.begin() + 1, client->argv_.end());

  client->SetKey(keys);
  return true;
}

void SInterCmd::DoCmd(PClient* client) {
  std::vector<std::string> res_vt;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SInter(client->Keys(), &res_vt);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "sinter cmd error");
    }
    return;
  }
  client->AppendStringVector(res_vt);
}

SRemCmd::SRemCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SRemCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SRemCmd::DoCmd(PClient* client) {
  std::vector<std::string> to_delete_members(client->argv_.begin() + 2, client->argv_.end());
  int32_t reply_num = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SRem(client->Key(), to_delete_members, &reply_num);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "srem cmd error");
    }
    return;
  }
  client->AppendInteger(reply_num);
}

SUnionCmd::SUnionCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SUnionCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin() + 1, client->argv_.end());
  client->SetKey(keys);
  return true;
}

void SUnionCmd::DoCmd(PClient* client) {
  std::vector<std::string> res_vt;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SUnion(client->Keys(), &res_vt);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "sunion cmd error");
    }
    return;
  }
  client->AppendStringVector(res_vt);
}

SInterStoreCmd::SInterStoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SInterStoreCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SInterStoreCmd::DoCmd(PClient* client) {
  std::vector<std::string> value_to_dest;
  int32_t reply_num = 0;

  std::vector<std::string> inter_keys(client->argv_.begin() + 2, client->argv_.end());
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->SInterstore(client->Key(), inter_keys, value_to_dest, &reply_num);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "sinterstore cmd error");
    }
    return;
  }
  client->AppendInteger(reply_num);
}

SCardCmd::SCardCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SCardCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}
void SCardCmd::DoCmd(PClient* client) {
  int32_t reply_Num = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SCard(client->Key(), &reply_Num);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "scard cmd error");
    }
    return;
  }
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_Num);
    return;
  }
  client->SetRes(CmdRes::kSyntaxErr, "scard cmd error");
}

SMoveCmd::SMoveCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SMoveCmd::DoInitial(PClient* client) { return true; }

void SMoveCmd::DoCmd(PClient* client) {
  int32_t reply_num = 0;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->SMove(client->argv_[1], client->argv_[2], client->argv_[3], &reply_num);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(reply_num);
  } else {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, "smove cmd error");
    }
    return;
  }
}

SRandMemberCmd::SRandMemberCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SRandMemberCmd::DoInitial(PClient* client) {
  if (client->argv_.size() > 3) {
    client->SetRes(CmdRes::kWrongNum, client->CmdName());
    return false;
  } else if (client->argv_.size() == 3) {
    try {
      this->num_rand = stoi(client->argv_[2]);
    } catch (const std::invalid_argument& e) {
      client->SetRes(CmdRes::kInvalidBitInt, "srandmember cmd should have integer num of count.");
      return false;
    }
  }
  return true;
}

void SRandMemberCmd::DoCmd(PClient* client) {
  std::vector<std::string> vec_ret;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SRandmember(client->argv_[1], this->num_rand, &vec_ret);
  if (s.ok()) {
    if (client->argv_.size() == 3) {
      client->AppendStringVector(vec_ret);
    } else if (client->argv_.size() == 2) {  // srand only needs to return one element
      client->AppendString(vec_ret[0]);
    }
    return;
  }
  if (!s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "srandmember cmd error");
    }
    return;
  }
  client->AppendString("");
}

SPopCmd::SPopCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SPopCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SPopCmd::DoCmd(PClient* client) {
  std::vector<std::string> delete_members;
  if ((client->argv_.size()) == 2) {
    int64_t cnt = 1;
    std::vector<std::string> delete_member;
    storage::Status s =
        PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SPop(client->Key(), &delete_member, cnt);
    if (!s.ok()) {
      if (s.IsInvalidArgument()) {
        client->SetRes(CmdRes::kMultiKey);
      } else {
        client->SetRes(CmdRes::kSyntaxErr, "spop cmd error");
      }
      return;
    }
    client->AppendString(delete_member[0]);

  } else if ((client->argv_.size()) == 3) {
    std::vector<std::string> delete_members;
    int64_t cnt = 1;
    if (client->argv_[2].find(".") != std::string::npos || !pstd::String2int(client->argv_[2], &cnt)) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }
    storage::Status s =
        PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SPop(client->Key(), &delete_members, cnt);
    if (!s.ok()) {
      if (s.IsInvalidArgument()) {
        client->SetRes(CmdRes::kMultiKey);
      } else {
        client->SetRes(CmdRes::kSyntaxErr, "spop cmd error");
      }
      return;
    }
    client->AppendStringVector(delete_members);

  } else {
    client->SetRes(CmdRes::kWrongNum, "spop");
    return;
  }
}

SMembersCmd::SMembersCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SMembersCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SMembersCmd::DoCmd(PClient* client) {
  std::vector<std::string> delete_members;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SMembers(client->Key(), &delete_members);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "smembers cmd error");
    }
    return;
  }
  client->AppendStringVector(delete_members);
}

SDiffCmd::SDiffCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SDiffCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SDiffCmd::DoCmd(PClient* client) {
  std::vector<std::string> diff_members;
  std::vector<std::string> diff_keys(client->argv_.begin() + 1, client->argv_.end());
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SDiff(diff_keys, &diff_members);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "sdiff cmd error");
    }
    return;
  }
  client->AppendStringVector(diff_members);
}

SDiffstoreCmd::SDiffstoreCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategorySet) {}

bool SDiffstoreCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SDiffstoreCmd::DoCmd(PClient* client) {
  std::vector<std::string> value_to_dest;
  int32_t reply_num = 0;
  std::vector<std::string> diffstore_keys(client->argv_.begin() + 2, client->argv_.end());
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->SDiffstore(client->Key(), diffstore_keys, value_to_dest, &reply_num);
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "sdiffstore cmd error");
    }
    return;
  }
  client->AppendInteger(reply_num);
}

SScanCmd::SScanCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategorySet) {}

bool SScanCmd::DoInitial(PClient* client) {
  if (auto size = client->argv_.size(); size != 3 && size != 5 && size != 7) {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void SScanCmd::DoCmd(PClient* client) {
  const auto& argv = client->argv_;
  // parse arguments
  int64_t cursor = 0;
  int64_t count = 10;
  std::string pattern{"*"};
  if (pstd::String2int(argv[2], &cursor) == 0) {
    client->SetRes(CmdRes::kInvalidCursor, kCmdNameSScan);
    return;
  }
  for (size_t i = 3; i < argv.size(); i += 2) {
    if (auto lower = pstd::StringToLower(argv[i]); kMatchSymbol == lower) {
      pattern = argv[i + 1];
    } else if (kCountSymbol == lower) {
      if (pstd::String2int(argv[i + 1], &count) == 0) {
        client->SetRes(CmdRes::kInvalidInt, kCmdNameSScan);
        return;
      }
      if (count < 0) {
        client->SetRes(CmdRes::kSyntaxErr, kCmdNameSScan);
        return;
      }
    } else {
      client->SetRes(CmdRes::kSyntaxErr, kCmdNameSScan);
      return;
    }
  }

  // execute command
  std::vector<std::string> members;
  int64_t next_cursor{};
  auto status = PSTORE.GetBackend(client->GetCurrentDB())
                    ->GetStorage()
                    ->SScan(client->Key(), cursor, pattern, count, &members, &next_cursor);
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
  client->AppendArrayLenUint64(members.size());
  for (const auto& member : members) {
    client->AppendString(member);
  }
}

}  // namespace pikiwidb
