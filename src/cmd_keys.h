// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  This file defines some functions and commands related
  to key operations.
 */

#pragma once

#include "base_cmd.h"

namespace pikiwidb {

class DelCmd : public BaseCmd {
 public:
  DelCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  rocksdb::Status s_;
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
};

class ExistsCmd : public BaseCmd {
 public:
  ExistsCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void ReadCache(PClient* client) override;
};

class TypeCmd : public BaseCmd {
 public:
  TypeCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void ReadCache(PClient* client) override;
};

class ExpireCmd : public BaseCmd {
 public:
  ExpireCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  rocksdb::Status s_;
  int64_t sec_ = 0;
};

class TtlCmd : public BaseCmd {
 public:
  TtlCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void ReadCache(PClient* client) override;
};

class PExpireCmd : public BaseCmd {
 public:
  PExpireCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
  int64_t msec_ = 0;
  rocksdb::Status s_;
};

class ExpireatCmd : public BaseCmd {
 public:
  ExpireatCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  rocksdb::Status s_;
  int64_t time_stamp_ = 0;
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
};

class PExpireatCmd : public BaseCmd {
 public:
  PExpireatCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  rocksdb::Status s_;
  int64_t time_stamp_ms_ = 0;
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
};

class PersistCmd : public BaseCmd {
 public:
  PersistCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  rocksdb::Status s_;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
};

class KeysCmd : public BaseCmd {
 public:
  KeysCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class PttlCmd : public BaseCmd {
 public:
  PttlCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void ReadCache(PClient* client) override;
};

class RenameCmd : public BaseCmd {
 public:
  RenameCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  rocksdb::Status s_;
  void DoCmd(PClient* client) override;
  void DoThroughDB(PClient* client) override;
  void DoUpdateCache(PClient* client) override;
};

class RenameNXCmd : public BaseCmd {
 public:
  RenameNXCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

}  // namespace pikiwidb
