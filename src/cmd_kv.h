// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of operation functions and commands related
  to key-value pairs.
 */

#pragma once

#include "base_cmd.h"

namespace pikiwidb {

class GetCmd : public BaseCmd {
 public:
  GetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  std::string value_;
  rocksdb::Status s_;
  int64_t ttl_ = 0;

  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
};

class SetCmd : public BaseCmd {
 public:
  enum SetCondition { kNONE, kNX, kXX, kEXORPX };
  SetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;

  std::string value_;
  bool has_ttl_ = false;
  std::string target_;
  int64_t sec_ = 0;
  SetCmd::SetCondition condition_{kNONE};
  rocksdb::Status s_;
};

class BitOpCmd : public BaseCmd {
 public:
  enum BitOp {
    kBitOpAnd,
    kBitOpOr,
    kBitOpNot,
    kBitOpXor,
  };
  BitOpCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class StrlenCmd : public BaseCmd {
 public:
  StrlenCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
  int64_t sec_ = 0;
  std::string value_;
};

class SetExCmd : public BaseCmd {
 public:
  SetExCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  int64_t sec_ = 0;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class PSetExCmd : public BaseCmd {
 public:
  PSetExCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  int64_t msec_ = 0;
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class SetNXCmd : public BaseCmd {
 public:
  SetNXCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class AppendCmd : public BaseCmd {
 public:
  AppendCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class GetSetCmd : public BaseCmd {
 public:
  GetSetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class MGetCmd : public BaseCmd {
 public:
  MGetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  std::vector<storage::ValueStatus> db_value_status_array_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
};

class MSetCmd : public BaseCmd {
 public:
  MSetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  std::vector<storage::KeyValue> kvs_;
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class BitCountCmd : public BaseCmd {
 public:
  BitCountCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class DecrCmd : public BaseCmd {
 public:
  DecrCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class IncrCmd : public BaseCmd {
 public:
  IncrCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  rocksdb::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class IncrbyCmd : public BaseCmd {
 public:
  IncrbyCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  rocksdb::Status s_;
  int64_t by_ = 0;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};
class DecrbyCmd : public BaseCmd {
 public:
  DecrbyCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  int64_t by_ = 0;
  storage::Status s_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class SetBitCmd : public BaseCmd {
 public:
  SetBitCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class GetBitCmd : public BaseCmd {
 public:
  GetBitCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class IncrbyFloatCmd : public BaseCmd {
 public:
  IncrbyFloatCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  std::string value_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class GetRangeCmd : public BaseCmd {
 public:
  GetRangeCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  int64_t start_ = 0;
  int64_t end_ = 0;
  storage::Status s_;
  std::string value_;
  int64_t sec_ = 0;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
};

class SetRangeCmd : public BaseCmd {
 public:
  SetRangeCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  storage::Status s_;
  int64_t offset_ = 0;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
};

class MSetnxCmd : public BaseCmd {
 public:
  MSetnxCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

}  // namespace pikiwidb
