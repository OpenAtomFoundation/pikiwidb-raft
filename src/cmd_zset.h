// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of features related to sorted sets.
 */

#pragma once
#include "base_cmd.h"

namespace pikiwidb {

class ZAddCmd : public BaseCmd {
 public:
  ZAddCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  std::string key_;
  std::vector<storage::ScoreMember> score_members_;
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class ZPopMinCmd : public BaseCmd {
 public:
  ZPopMinCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class ZPopMaxCmd : public BaseCmd {
 public:
  ZPopMaxCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class ZsetUIstoreParentCmd : public BaseCmd {
 public:
  ZsetUIstoreParentCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

  std::string dest_key_;
  int64_t num_keys_ = 0;
  storage::AGGREGATE aggregate_{storage::SUM};
  std::vector<std::string> keys_;
  std::vector<double> weights_;
};

class ZInterstoreCmd : public ZsetUIstoreParentCmd {
 public:
  ZInterstoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class ZUnionstoreCmd : public ZsetUIstoreParentCmd {
 public:
  ZUnionstoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

class ZRevrangeCmd : public BaseCmd {
 public:
  ZRevrangeCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRangebyscoreCmd : public BaseCmd {
 public:
  ZRangebyscoreCmd(const std::string &name, int16_t arity);
  int64_t Offset() { return offset_; }
  int64_t Count() { return count_; }
  void ResetCount() { count_ = -1; }

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
  double min_score_ = 0, max_score_ = 0;
  bool left_close_ = true, right_close_ = true, with_scores_ = false;
  int64_t offset_ = 0, count_ = -1;
};

class ZRemrangebyrankCmd : public BaseCmd {
 public:
  ZRemrangebyrankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  int32_t start_ = 0;
  int32_t end_ = 0;
};

class ZRevrangebyscoreCmd : public BaseCmd {
 public:
  ZRevrangebyscoreCmd(const std::string &name, int16_t arity);
  int64_t Offset() { return offset_; }
  int64_t Count() { return count_; }
  void ResetCount() { count_ = -1; }

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
  double min_score_ = 0, max_score_ = 0;
  bool left_close_ = true, right_close_ = true, with_scores_ = false;
  int64_t offset_ = 0, count_ = -1;
};

class ZCardCmd : public BaseCmd {
 public:
  ZCardCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class ZRangeCmd : public BaseCmd {
 public:
  ZRangeCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZScoreCmd : public BaseCmd {
 public:
  ZScoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class ZRangebylexCmd : public BaseCmd {
 public:
  ZRangebylexCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
  std::string min_member_, max_member_;
  bool left_close_ = true, right_close_ = true;
  int64_t offset_ = 0, count_ = -1;
};

class ZRevrangebylexCmd : public BaseCmd {
 public:
  ZRevrangebylexCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
  std::string min_member_, max_member_;
  bool left_close_ = true, right_close_ = true;
  int64_t offset_ = 0, count_ = -1;
};

class ZRankCmd : public BaseCmd {
 public:
  ZRankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class ZRevrankCmd : public BaseCmd {
 public:
  ZRevrankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  void ReadCache(PClient *client) override;
  storage::Status s_;
};

class ZRemCmd : public BaseCmd {
 public:
  ZRemCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  int32_t deleted_ = 0;
};

class ZIncrbyCmd : public BaseCmd {
 public:
  ZIncrbyCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
  double by_ = .0f;
  double score_ = .0f;
};

class ZRemrangebyscoreCmd : public BaseCmd {
 public:
  ZRemrangebyscoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoThroughDB(PClient *client) override;
  void DoUpdateCache(PClient *client) override;
  storage::Status s_;
};

}  // namespace pikiwidb
