// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Declared a set of functions for operating key-value pairs
  in a hash table, which can be understood with reference to the principles of Redis HSET and other commands.
 */

#pragma once

#include <string_view>
#include "base_cmd.h"

namespace pikiwidb {

class HSetCmd : public BaseCmd {
 public:
  HSetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class HGetCmd : public BaseCmd {
 public:
  HGetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class HDelCmd : public BaseCmd {
 public:
  HDelCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  rocksdb::Status s_;
  int32_t deleted_ = 0;
};

class HMSetCmd : public BaseCmd {
 public:
  HMSetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  rocksdb::Status s_;
};

class HMGetCmd : public BaseCmd {
 public:
  HMGetCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
};

class HGetAllCmd : public BaseCmd {
 public:
  HGetAllCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
};

class HKeysCmd : public BaseCmd {
 public:
  HKeysCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
};

class HLenCmd : public BaseCmd {
 public:
  HLenCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
};

class HStrLenCmd : public BaseCmd {
 public:
  HStrLenCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class HScanCmd : public BaseCmd {
 public:
  HScanCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;

  static constexpr const char *kMatchSymbol = "match";
  static constexpr const char *kCountSymbol = "count";
};

class HValsCmd : public BaseCmd {
 public:
  HValsCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class HIncrbyFloatCmd : public BaseCmd {
 public:
  HIncrbyFloatCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class HSetNXCmd : public BaseCmd {
 public:
  HSetNXCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class HIncrbyCmd : public BaseCmd {
 public:
  HIncrbyCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  int64_t int_by_ = 0;
};

class HRandFieldCmd : public BaseCmd {
 public:
  HRandFieldCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  static constexpr std::string_view kWithValueString = "withvalues";
};

class HExistsCmd : public BaseCmd {
 public:
  HExistsCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  rocksdb::Status s_;
};

}  // namespace pikiwidb
