// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Declarations of a set of instructions and functions related
  to set operations.
 */

#pragma once
#include "base_cmd.h"

namespace pikiwidb {

class SIsMemberCmd : public BaseCmd {
 public:
  SIsMemberCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class SAddCmd : public BaseCmd {
 public:
  SAddCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class SUnionStoreCmd : public BaseCmd {
 public:
  SUnionStoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class SRemCmd : public BaseCmd {
 public:
  SRemCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  int32_t deleted_num = 0;
};

class SUnionCmd : public BaseCmd {
 public:
  SUnionCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class SInterCmd : public BaseCmd {
 public:
  SInterCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class SInterStoreCmd : public BaseCmd {
 public:
  SInterStoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class SCardCmd : public BaseCmd {
 public:
  SCardCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class SMoveCmd : public BaseCmd {
 public:
  SMoveCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class SRandMemberCmd : public BaseCmd {
 public:
  SRandMemberCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  int num_rand = 1;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class SPopCmd : public BaseCmd {
 public:
  SPopCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  std::vector<std::string> deleted_members_;
};

class SMembersCmd : public BaseCmd {
 public:
  SMembersCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class SDiffCmd : public BaseCmd {
 public:
  SDiffCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class SDiffstoreCmd : public BaseCmd {
 public:
  SDiffstoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class SScanCmd : public BaseCmd {
 public:
  SScanCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;

  static constexpr const char *kMatchSymbol = "match";
  static constexpr const char *kCountSymbol = "count";
};

}  // namespace pikiwidb
