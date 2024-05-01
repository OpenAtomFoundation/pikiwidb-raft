/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "base_cmd.h"
#include "config.h"

const std::vector<std::string> debugHelps = {"DEBUG <subcommand> [<arg> [value] [opt] ...]. Subcommands are:",
                                             "HELP",
                                             "    Print this help.",
                                             "SEGFAULT",
                                             "    Crash the server with sigsegv.",
                                             "OOM",
                                             "    Crash the server simulating an out-of-memory error."};

namespace pikiwidb {

class CmdConfig : public BaseCmdGroup {
 public:
  CmdConfig(const std::string& name, int arity);

  bool HasSubCommand() const override;

 protected:
  bool DoInitial(PClient* client) override { return true; };

 private:
  //  std::vector<std::string> subCmd_;

  void DoCmd(PClient* client) override{};
};

class CmdConfigGet : public BaseCmd {
 public:
  CmdConfigGet(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class CmdConfigSet : public BaseCmd {
 public:
  CmdConfigSet(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class FlushdbCmd : public BaseCmd {
 public:
  FlushdbCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class FlushallCmd : public BaseCmd {
 public:
  FlushallCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class SelectCmd : public BaseCmd {
 public:
  SelectCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class ShutdownCmd : public BaseCmd {
 public:
  ShutdownCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class PingCmd : public BaseCmd {
 public:
  PingCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class InfoCmd : public BaseCmd {
 public:
  InfoCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;

  enum InfoSection {
    kInfoErr = 0x0,
    kInfoServer,
    kInfoStats,
    kInfoCPU,
    kInfoData,
    kInfo,
    kInfoAll,
    kInfoCommandStats,
    kInfoRaft
  };

  InfoSection info_section_;
  const static std::string kInfoSection;
  const static std::string kAllSection;
  const static std::string kServerSection;
  const static std::string kClientsSection;
  const static std::string kStatsSection;
  const static std::string kExecCountSection;
  const static std::string kCPUSection;
  const static std::string kReplicationSection;
  const static std::string kKeyspaceSection;
  const static std::string kDataSection;
  const static std::string kRocksDBSection;
  const static std::string kDebugSection;
  const static std::string kCommandStatsSection;
  const static std::string kCacheSection;
  const static std::string kRaftSection;

  void InfoServer(std::string& info);
  void InfoStats(std::string& info);
  void InfoCPU(std::string& info);
  void InfoRaft(std::string& info);
  void InfoData(std::string& info);
  void InfoCommandStats(PClient* client, std::string& info);

  double MethodofTotalTimeCalculation(const uint64_t time_consuming);
  double MethodofCommandStatistics(const uint64_t time_consuming, const uint64_t frequency);
};

class CmdDebug : public BaseCmdGroup {
 public:
  CmdDebug(const std::string& name, int arity);

  bool HasSubCommand() const override;

 protected:
  bool DoInitial(PClient* client) override { return true; };

 private:
  void DoCmd(PClient* client) override{};
};

class CmdDebugHelp : public BaseCmd {
 public:
  CmdDebugHelp(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class CmdDebugOOM : public BaseCmd {
 public:
  CmdDebugOOM(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class CmdDebugSegfault : public BaseCmd {
 public:
  CmdDebugSegfault(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};
}  // namespace pikiwidb
