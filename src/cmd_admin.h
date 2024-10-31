// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Stores the declarations of management commands.
 */

#pragma once

#include <optional>
#include <variant>
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
const std::string kCmdNameMonitor = "monitor";

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
  const static std::string kStatsSection;
  const static std::string kCPUSection;
  const static std::string kDataSection;
  const static std::string kCommandStatsSection;
  const static std::string kRaftSection;

  const std::unordered_map<std::string, InfoSection> sectionMap = {{kAllSection, kInfoAll},
                                                                   {kServerSection, kInfoServer},
                                                                   {kStatsSection, kInfoStats},
                                                                   {kCPUSection, kInfoCPU},
                                                                   {kDataSection, kInfoData},
                                                                   {kRaftSection, kInfoRaft},
                                                                   {kCommandStatsSection, kInfoCommandStats}};

  void InfoServer(std::string& info);
  void InfoStats(std::string& info);
  void InfoCPU(std::string& info);
  void InfoRaft(std::string& info);
  void InfoData(std::string& info);
  void InfoCommandStats(PClient* client, std::string& info);
  std::string FormatCommandStatLine(const CommandStatistics& stats);
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

class MonitorCmd : public BaseCmd {
 public:
  MonitorCmd(const std::string& name, int arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;
};

class SortCmd : public BaseCmd {
 public:
  SortCmd(const std::string& name, int16_t arity);

 protected:
  bool DoInitial(PClient* client) override;

 private:
  void DoCmd(PClient* client) override;

  void InitialArgument();
  std::optional<std::string> lookupKeyByPattern(PClient* client, const std::string& pattern, const std::string& subst);

  struct RedisSortObject {
    std::string obj;
    std::variant<double, std::string> u;
  };

  int desc_ = 0;
  int alpha_ = 0;
  size_t offset_ = 0;
  size_t count_ = -1;
  int dontsort_ = 0;
  std::string store_key_;
  std::string sortby_;
  std::vector<std::string> get_patterns_;
  std::vector<std::string> ret_;
};

}  // namespace pikiwidb
