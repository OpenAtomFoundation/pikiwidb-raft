/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
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
};

class ZPopMinCmd : public BaseCmd {
 public:
  ZPopMinCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZPopMaxCmd : public BaseCmd {
 public:
  ZPopMaxCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
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
};

class ZUnionstoreCmd : public ZsetUIstoreParentCmd {
 public:
  ZUnionstoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
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

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRemrangebyrankCmd : public BaseCmd {
 public:
  ZRemrangebyrankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRevrangebyscoreCmd : public BaseCmd {
 public:
  ZRevrangebyscoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZCardCmd : public BaseCmd {
 public:
  ZCardCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
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
};

class ZRangebylexCmd : public BaseCmd {
 public:
  ZRangebylexCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRevrangebylexCmd : public BaseCmd {
 public:
  ZRevrangebylexCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRankCmd : public BaseCmd {
 public:
  ZRankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRevrankCmd : public BaseCmd {
 public:
  ZRevrankCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRemCmd : public BaseCmd {
 public:
  ZRemCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZIncrbyCmd : public BaseCmd {
 public:
  ZIncrbyCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

class ZRemrangebyscoreCmd : public BaseCmd {
 public:
  ZRemrangebyscoreCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
};

}  // namespace pikiwidb
