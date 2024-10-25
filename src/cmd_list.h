// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of operation functions and commands related to lists.
 */

#pragma once
#include "base_cmd.h"

namespace pikiwidb {
class LPushCmd : public BaseCmd {
 public:
  LPushCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};

class RPushCmd : public BaseCmd {
 public:
  RPushCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};

class RPopCmd : public BaseCmd {
 public:
  RPopCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};
class LRangeCmd : public BaseCmd {
 public:
  LRangeCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  void ReadCache(PClient* client) override;
  int64_t start_index_ = 0;
  int64_t end_index_ = 0;
  storage::Status s_;
};

class LRemCmd : public BaseCmd {
 public:
  LRemCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
  int64_t freq_ = 0;
};

class LTrimCmd : public BaseCmd {
 public:
  LTrimCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
  int64_t start_index_ = 0;
  int64_t end_index_ = 0;
};

class LSetCmd : public BaseCmd {
 public:
  LSetCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
  int64_t index_ = 0;
};

class LInsertCmd : public BaseCmd {
 public:
  LInsertCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage ::BeforeOrAfter before_or_after_;
  storage::Status s_;
};

class LPushxCmd : public BaseCmd {
 public:
  LPushxCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};

class RPushxCmd : public BaseCmd {
 public:
  RPushxCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};

class RPoplpushCmd : public BaseCmd {
 public:
  RPoplpushCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;

 private:
  std::string source_;
  std::string receiver_;
  std::string value_poped_from_source_;
};

class LPopCmd : public BaseCmd {
 public:
  LPopCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  storage::Status s_;
};

class LIndexCmd : public BaseCmd {
 public:
  LIndexCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  void ReadCache(PClient* client) override;
  int64_t index_ = 0;
  storage::Status s_;
};

class LLenCmd : public BaseCmd {
 public:
  LLenCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  void ReadCache(PClient* client) override;
  storage::Status s_;
};
}  // namespace pikiwidb
