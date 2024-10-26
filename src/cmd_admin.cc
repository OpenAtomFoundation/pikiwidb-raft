// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
   Many management-level commands are defined here.

   Compared to the external commands at the user level,
   the commands defined here are more focused on the overall
   management of the PikiwiDB.

 */
#include "cmd_admin.h"

#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <string>
#include <vector>

#include "braft/raft.h"
#include "rocksdb/version.h"

#include "praft/praft.h"
#include "pstd/env.h"
#include "pstd/pstd_string.h"

#include "db.h"
#include "pikiwidb.h"
#include "slow_log.h"
#include "store.h"

namespace pikiwidb {

CmdConfig::CmdConfig(const std::string& name, int arity) : BaseCmdGroup(name, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfig::HasSubCommand() const { return true; }

CmdConfigGet::CmdConfigGet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdConfigGet::DoInitial(PClient* client) { return true; }

void CmdConfigGet::DoCmd(PClient* client) {
  std::vector<std::string> results;
  for (int i = 0; i < client->argv_.size() - 2; i++) {
    g_config.Get(client->argv_[i + 2], &results);
  }
  client->AppendStringVector(results);
}

CmdConfigSet::CmdConfigSet(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdConfigSet::DoInitial(PClient* client) { return true; }

void CmdConfigSet::DoCmd(PClient* client) {
  auto s = g_config.Set(client->argv_[2], client->argv_[3]);
  if (!s.ok()) {
    client->SetRes(CmdRes::kInvalidParameter);
  } else {
    client->SetRes(CmdRes::kOK);
  }
}

FlushdbCmd::FlushdbCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsExclusive | kCmdFlagsAdmin | kCmdFlagsWrite,
              kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushdbCmd::DoInitial(PClient* client) { return true; }

void FlushdbCmd::DoCmd(PClient* client) {
  int currentDBIndex = client->GetCurrentDB();
  PSTORE.GetBackend(currentDBIndex).get()->Lock();
  DEFER { PSTORE.GetBackend(currentDBIndex).get()->UnLock(); };

  std::string db_path = g_config.db_path.ToString() + std::to_string(currentDBIndex);
  std::string path_temp = db_path;
  path_temp.append("_deleting/");
  pstd::RenameFile(db_path, path_temp);

  auto s = PSTORE.GetBackend(currentDBIndex)->Open();
  if (!s.ok()) {
    client->SetRes(CmdRes::kErrOther, "flushdb failed");
    return;
  }
  auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
  client->SetRes(CmdRes::kOK);
}

FlushallCmd::FlushallCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsExclusive | kCmdFlagsAdmin | kCmdFlagsWrite,
              kAclCategoryWrite | kAclCategoryAdmin) {}

bool FlushallCmd::DoInitial(PClient* client) { return true; }

void FlushallCmd::DoCmd(PClient* client) {
  for (size_t i = 0; i < g_config.databases; ++i) {
    PSTORE.GetBackend(i).get()->Lock();
    std::string db_path = g_config.db_path.ToString() + std::to_string(i);
    std::string path_temp = db_path;
    path_temp.append("_deleting/");
    pstd::RenameFile(db_path, path_temp);

    auto s = PSTORE.GetBackend(i)->Open();
    assert(s.ok());
    auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
    PSTORE.GetBackend(i).get()->UnLock();
  }
  client->SetRes(CmdRes::kOK);
}

SelectCmd::SelectCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool SelectCmd::DoInitial(PClient* client) { return true; }

void SelectCmd::DoCmd(PClient* client) {
  int index = atoi(client->argv_[1].c_str());
  if (index < 0 || index >= g_config.databases) {
    client->SetRes(CmdRes::kInvalidIndex, kCmdNameSelect + " DB index is out of range");
    return;
  }
  client->SetCurrentDB(index);
  client->SetRes(CmdRes::kOK);
}

ShutdownCmd::ShutdownCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin | kAclCategoryWrite) {}

bool ShutdownCmd::DoInitial(PClient* client) {
  // For now, only shutdown need check local
  if (client->PeerIP().find("127.0.0.1") == std::string::npos &&
      client->PeerIP().find(g_config.ip.ToString()) == std::string::npos) {
    client->SetRes(CmdRes::kErrOther, kCmdNameShutdown + " should be localhost");
    return false;
  }
  return true;
}

void ShutdownCmd::DoCmd(PClient* client) {
  PSTORE.GetBackend(client->GetCurrentDB())->UnLockShared();
  g_pikiwidb->Stop();
  PSTORE.GetBackend(client->GetCurrentDB())->LockShared();
  client->SetRes(CmdRes::kNone);
}

PingCmd::PingCmd(const std::string& name, int16_t arity) : BaseCmd(name, arity, kCmdFlagsFast, kAclCategoryFast) {}

bool PingCmd::DoInitial(PClient* client) { return true; }

void PingCmd::DoCmd(PClient* client) { client->SetRes(CmdRes::kPong, "PONG"); }

const std::string InfoCmd::kInfoSection = "info";
const std::string InfoCmd::kAllSection = "all";
const std::string InfoCmd::kServerSection = "server";
const std::string InfoCmd::kStatsSection = "stats";
const std::string InfoCmd::kCPUSection = "cpu";
const std::string InfoCmd::kDataSection = "data";
const std::string InfoCmd::kCommandStatsSection = "commandstats";
const std::string InfoCmd::kRaftSection = "raft";

InfoCmd::InfoCmd(const std::string& name, int16_t arity) : BaseCmd(name, arity, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool InfoCmd::DoInitial(PClient* client) {
  size_t argc = client->argv_.size();
  if (argc == 1) {
    info_section_ = kInfo;
    return true;
  }

  auto argv = client->argv_[1];
  // convert section to lowercase
  std::transform(argv.begin(), argv.end(), argv.begin(), [](unsigned char c) { return std::tolower(c); });
  if (argc == 2) {
    auto it = sectionMap.find(argv);
    if (it != sectionMap.end()) {
      info_section_ = it->second;
    } else {
      client->SetRes(CmdRes::kErrOther, "the cmd is not supported");
      return false;
    }
  } else {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }
  return true;
}

void InfoCmd::DoCmd(PClient* client) {
  std::string info;
  switch (info_section_) {
    case kInfo:
      InfoServer(info);
      info.append("\r\n");
      InfoData(info);
      info.append("\r\n");
      InfoStats(info);
      info.append("\r\n");
      InfoCPU(info);
      info.append("\r\n");
      break;
    case kInfoAll:
      InfoServer(info);
      info.append("\r\n");
      InfoData(info);
      info.append("\r\n");
      InfoStats(info);
      info.append("\r\n");
      InfoCommandStats(client, info);
      info.append("\r\n");
      InfoCPU(info);
      info.append("\r\n");
      break;
    case kInfoServer:
      InfoServer(info);
      break;
    case kInfoStats:
      InfoStats(info);
      break;
    case kInfoCPU:
      InfoCPU(info);
      break;
    case kInfoData:
      InfoData(info);
      break;
    case kInfoCommandStats:
      InfoCommandStats(client, info);
      break;
    case kInfoRaft:
      InfoRaft(client, info);
      break;
    default:
      break;
  }

  client->AppendString(info);
}

/*
* INFO raft
* Querying Node Information.
* Reply:
*   raft_node_id:595100767
    raft_state:up
    raft_role:follower
    raft_is_voting:yes
    raft_leader_id:1733428433
    raft_current_term:1
    raft_num_nodes:2
    raft_num_voting_nodes:2
    raft_node1:id=1733428433,state=connected,voting=yes,addr=localhost,port=5001,last_conn_secs=5,conn_errors=0,conn_oks=1
*/
void InfoCmd::InfoRaft(PClient* client, std::string& message) {
  if (client->argv_.size() != 2) {
    return client->SetRes(CmdRes::kWrongNum, client->CmdName());
  }

  auto praft = PSTORE.GetBackend(client->GetCurrentDB())->GetPRaft();
  assert(praft);
  if (!praft->IsInitialized()) {
    return client->SetRes(CmdRes::kErrOther, "Don't already cluster member");
  }

  auto node_status = praft->GetNodeStatus();
  if (node_status.state == braft::State::STATE_END) {
    message += "-ERR Node is not initialized.\r\n";
    return;
  }

  std::stringstream tmp_stream;
  tmp_stream << "raft_group_id:" << praft->GetGroupID() << "\r\n";
  tmp_stream << "raft_node_id:" << praft->GetNodeID() << "\r\n";
  tmp_stream << "raft_peer_id:" << praft->GetPeerID() << "\r\n";
  if (braft::is_active_state(node_status.state)) {
    tmp_stream << "raft_state:up\r\n";
  } else {
    tmp_stream << "raft_state:down\r\n";
  }
  tmp_stream << "raft_role:" << std::string(braft::state2str(node_status.state)) << "\r\n";
  tmp_stream << "raft_leader_id:" << node_status.leader_id.to_string() << "\r\n";
  tmp_stream << "raft_current_term:" << std::to_string(node_status.term) << "\r\n";

  if (praft->IsLeader()) {
    std::vector<braft::PeerId> peers;
    auto status = praft->GetListPeers(&peers);
    if (!status.ok()) {
      tmp_stream.str("-ERR ");
      tmp_stream << status.error_str() << "\r\n";
      return;
    }

    for (int i = 0; i < peers.size(); i++) {
      tmp_stream << "raft_node" << std::to_string(i) << ":addr=" << butil::ip2str(peers[i].addr.ip).c_str()
                 << ",port=" << std::to_string(peers[i].addr.port) << "\r\n";
    }
  }

  message.append(tmp_stream.str());
}

void InfoCmd::InfoServer(std::string& info) {
  static struct utsname host_info;
  static bool host_info_valid = false;
  if (!host_info_valid) {
    uname(&host_info);
    host_info_valid = true;
  }

  time_t current_time_s = time(nullptr);
  std::stringstream tmp_stream;
  char version[32];
  snprintf(version, sizeof(version), "%s", KPIKIWIDB_VERSION);

  tmp_stream << "# Server\r\n";
  tmp_stream << "PikiwiDB_version:" << version << "\r\n";
#ifdef KPIKIWIDB_GIT_COMMIT_ID
  tmp_stream << "PikiwiDB_build_git_sha:" << KPIKIWIDB_GIT_COMMIT_ID << "\r\n";
#endif
#ifdef KPIKIWIDB_BUILD_DATE
  tmp_stream << "Pikiwidb_build_compile_date: " << KPIKIWIDB_BUILD_DATE << "\r\n";
#endif
  tmp_stream << "os:" << host_info.sysname << " " << host_info.release << " " << host_info.machine << "\r\n";
  tmp_stream << "arch_bits:" << (reinterpret_cast<char*>(&host_info.machine) + strlen(host_info.machine) - 2) << "\r\n";
  tmp_stream << "process_id:" << getpid() << "\r\n";
  tmp_stream << "run_id:" << static_cast<std::string>(g_config.run_id) << "\r\n";
  tmp_stream << "tcp_port:" << g_config.port << "\r\n";
  tmp_stream << "uptime_in_seconds:" << (current_time_s - g_pikiwidb->Start_time_s()) << "\r\n";
  tmp_stream << "uptime_in_days:" << (current_time_s / (24 * 3600) - g_pikiwidb->Start_time_s() / (24 * 3600) + 1)
             << "\r\n";
  tmp_stream << "config_file:" << g_pikiwidb->GetConfigName() << "\r\n";

  info.append(tmp_stream.str());
}

void InfoCmd::InfoStats(std::string& info) {
  std::stringstream tmp_stream;
  tmp_stream << "# Stats"
             << "\r\n";

  tmp_stream << "is_bgsaving:" << (PREPL.IsBgsaving() ? "Yes" : "No") << "\r\n";
  tmp_stream << "slow_logs_count:" << PSlowLog::Instance().GetLogsCount() << "\r\n";
  info.append(tmp_stream.str());
}

void InfoCmd::InfoCPU(std::string& info) {
  struct rusage self_ru;
  struct rusage c_ru;
  getrusage(RUSAGE_SELF, &self_ru);
  getrusage(RUSAGE_CHILDREN, &c_ru);
  std::stringstream tmp_stream;
  tmp_stream << "# CPU"
             << "\r\n";
  tmp_stream << "used_cpu_sys:" << std::setiosflags(std::ios::fixed) << std::setprecision(2)
             << static_cast<float>(self_ru.ru_stime.tv_sec) + static_cast<float>(self_ru.ru_stime.tv_usec) / 1000000
             << "\r\n";
  tmp_stream << "used_cpu_user:" << std::setiosflags(std::ios::fixed) << std::setprecision(2)
             << static_cast<float>(self_ru.ru_utime.tv_sec) + static_cast<float>(self_ru.ru_utime.tv_usec) / 1000000
             << "\r\n";
  tmp_stream << "used_cpu_sys_children:" << std::setiosflags(std::ios::fixed) << std::setprecision(2)
             << static_cast<float>(c_ru.ru_stime.tv_sec) + static_cast<float>(c_ru.ru_stime.tv_usec) / 1000000
             << "\r\n";
  tmp_stream << "used_cpu_user_children:" << std::setiosflags(std::ios::fixed) << std::setprecision(2)
             << static_cast<float>(c_ru.ru_utime.tv_sec) + static_cast<float>(c_ru.ru_utime.tv_usec) / 1000000
             << "\r\n";
  info.append(tmp_stream.str());
}

void InfoCmd::InfoData(std::string& message) {
  message += DATABASES_NUM + std::string(":") + std::to_string(pikiwidb::g_config.databases) + "\r\n";
  message += ROCKSDB_NUM + std::string(":") + std::to_string(pikiwidb::g_config.db_instance_num) + "\r\n";
  message += ROCKSDB_VERSION + std::string(":") + ROCKSDB_NAMESPACE::GetRocksVersionAsString() + "\r\n";
}

double InfoCmd::MethodofTotalTimeCalculation(const uint64_t time_consuming) {
  return static_cast<double>(time_consuming) / 1000.0;
}

double InfoCmd::MethodofCommandStatistics(const uint64_t time_consuming, const uint64_t frequency) {
  return (static_cast<double>(time_consuming) / 1000.0) / static_cast<double>(frequency);
}

void InfoCmd::InfoCommandStats(PClient* client, std::string& info) {
  std::stringstream tmp_stream;
  tmp_stream.precision(2);
  tmp_stream.setf(std::ios::fixed);
  tmp_stream << "# Commandstats"
             << "\r\n";
  auto cmdstat_map = client->GetCommandStatMap();
  for (auto iter : *cmdstat_map) {
    if (iter.second.cmd_count_ != 0) {
      tmp_stream << iter.first << ":" << FormatCommandStatLine(iter.second);
    }
  }
  info.append(tmp_stream.str());
}

std::string InfoCmd::FormatCommandStatLine(const CommandStatistics& stats) {
  std::stringstream stream;
  stream.precision(2);
  stream.setf(std::ios::fixed);
  stream << "calls=" << stats.cmd_count_ << ", usec=" << MethodofTotalTimeCalculation(stats.cmd_time_consuming_)
         << ", usec_per_call=";
  if (!stats.cmd_time_consuming_) {
    stream << 0 << "\r\n";
  } else {
    stream << MethodofCommandStatistics(stats.cmd_time_consuming_, stats.cmd_count_) << "\r\n";
  }
  return stream.str();
}

CmdDebug::CmdDebug(const std::string& name, int arity) : BaseCmdGroup(name, kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool CmdDebug::HasSubCommand() const { return true; }

CmdDebugHelp::CmdDebugHelp(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugHelp::DoInitial(PClient* client) { return true; }

void CmdDebugHelp::DoCmd(PClient* client) { client->AppendStringVector(debugHelps); }

CmdDebugOOM::CmdDebugOOM(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugOOM::DoInitial(PClient* client) { return true; }

void CmdDebugOOM::DoCmd(PClient* client) {
  auto ptr = ::operator new(std::numeric_limits<unsigned long>::max());
  ::operator delete(ptr);
  client->SetRes(CmdRes::kErrOther);
}

CmdDebugSegfault::CmdDebugSegfault(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool CmdDebugSegfault::DoInitial(PClient* client) { return true; }

void CmdDebugSegfault::DoCmd(PClient* client) {
  auto ptr = reinterpret_cast<int*>(0);
  *ptr = 0;
}

SortCmd::SortCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsWrite, kAclCategoryAdmin) {}

bool SortCmd::DoInitial(PClient* client) {
  InitialArgument();
  client->SetKey(client->argv_[1]);
  size_t argc = client->argv_.size();
  for (int i = 2; i < argc; ++i) {
    int leftargs = argc - i - 1;
    if (strcasecmp(client->argv_[i].data(), "asc") == 0) {
      desc_ = 0;
    } else if (strcasecmp(client->argv_[i].data(), "desc") == 0) {
      desc_ = 1;
    } else if (strcasecmp(client->argv_[i].data(), "alpha") == 0) {
      alpha_ = 1;
    } else if (strcasecmp(client->argv_[i].data(), "limit") == 0 && leftargs >= 2) {
      if (pstd::String2int(client->argv_[i + 1], &offset_) == 0 ||
          pstd::String2int(client->argv_[i + 2], &count_) == 0) {
        client->SetRes(CmdRes::kSyntaxErr);
        return false;
      }
      i += 2;
    } else if (strcasecmp(client->argv_[i].data(), "store") == 0 && leftargs >= 1) {
      store_key_ = client->argv_[i + 1];
      i++;
    } else if (strcasecmp(client->argv_[i].data(), "by") == 0 && leftargs >= 1) {
      sortby_ = client->argv_[i + 1];
      if (sortby_.find('*') == std::string::npos) {
        dontsort_ = 1;
      }
      i++;
    } else if (strcasecmp(client->argv_[i].data(), "get") == 0 && leftargs >= 1) {
      get_patterns_.push_back(client->argv_[i + 1]);
      i++;
    } else {
      client->SetRes(CmdRes::kSyntaxErr);
      return false;
    }
  }

  Status s;
  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->LRange(client->Key(), 0, -1, &ret_);
  if (s.ok()) {
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }

  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->SMembers(client->Key(), &ret_);
  if (s.ok()) {
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }

  std::vector<storage::ScoreMember> score_members;
  s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZRange(client->Key(), 0, -1, &score_members);
  if (s.ok()) {
    for (auto& c : score_members) {
      ret_.emplace_back(c.member);
    }
    return true;
  } else if (!s.IsNotFound()) {
    client->SetRes(CmdRes::kErrOther, s.ToString());
    return false;
  }
  client->SetRes(CmdRes::kErrOther, "Unknown Type");
  return false;
}

void SortCmd::DoCmd(PClient* client) {
  std::vector<RedisSortObject> sort_ret(ret_.size());
  for (size_t i = 0; i < ret_.size(); ++i) {
    sort_ret[i].obj = ret_[i];
  }

  if (!dontsort_) {
    for (size_t i = 0; i < ret_.size(); ++i) {
      std::string byval;
      if (!sortby_.empty()) {
        auto lookup = lookupKeyByPattern(client, sortby_, ret_[i]);
        if (!lookup.has_value()) {
          byval = ret_[i];
        } else {
          byval = std::move(lookup.value());
        }
      } else {
        byval = ret_[i];
      }

      if (alpha_) {
        sort_ret[i].u = byval;
      } else {
        double double_byval;
        if (pstd::String2d(byval, &double_byval)) {
          sort_ret[i].u = double_byval;
        } else {
          client->SetRes(CmdRes::kErrOther, "One or more scores can't be converted into double");
          return;
        }
      }
    }

    std::sort(sort_ret.begin(), sort_ret.end(), [this](const RedisSortObject& a, const RedisSortObject& b) {
      if (this->alpha_) {
        std::string score_a = std::get<std::string>(a.u);
        std::string score_b = std::get<std::string>(b.u);
        return !this->desc_ ? score_a < score_b : score_a > score_b;
      } else {
        double score_a = std::get<double>(a.u);
        double score_b = std::get<double>(b.u);
        return !this->desc_ ? score_a < score_b : score_a > score_b;
      }
    });

    size_t sort_size = sort_ret.size();

    count_ = count_ >= 0 ? count_ : sort_size;
    offset_ = (offset_ >= 0 && offset_ < sort_size) ? offset_ : sort_size;
    count_ = (offset_ + count_ < sort_size) ? count_ : sort_size - offset_;

    size_t m_start = offset_;
    size_t m_end = offset_ + count_;

    ret_.clear();
    if (get_patterns_.empty()) {
      get_patterns_.emplace_back("#");
    }

    for (; m_start < m_end; m_start++) {
      for (const std::string& pattern : get_patterns_) {
        std::optional<std::string> val = lookupKeyByPattern(client, pattern, sort_ret[m_start].obj);
        if (val.has_value()) {
          ret_.push_back(val.value());
        } else {
          ret_.emplace_back("");
        }
      }
    }
  }

  if (store_key_.empty()) {
    client->AppendStringVector(ret_);
  } else {
    uint64_t reply_num = 0;
    storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->RPush(store_key_, ret_, &reply_num);
    if (s.ok()) {
      client->AppendInteger(reply_num);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
  }
}

std::optional<std::string> SortCmd::lookupKeyByPattern(PClient* client, const std::string& pattern,
                                                       const std::string& subst) {
  if (pattern == "#") {
    return subst;
  }

  auto match_pos = pattern.find('*');
  if (match_pos == std::string::npos) {
    return std::nullopt;
  }

  std::string field;
  auto arrow_pos = pattern.find("->", match_pos + 1);
  if (arrow_pos != std::string::npos && arrow_pos + 2 < pattern.size()) {
    field = pattern.substr(arrow_pos + 2);
  }

  std::string key = pattern.substr(0, match_pos + 1);
  key.replace(match_pos, 1, subst);

  std::string value;
  storage::Status s;
  if (!field.empty()) {
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HGet(key, field, &value);
  } else {
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->Get(key, &value);
  }

  if (!s.ok()) {
    return std::nullopt;
  }

  return value;
}

void SortCmd::InitialArgument() {
  desc_ = 0;
  alpha_ = 0;
  offset_ = 0;
  count_ = -1;
  dontsort_ = 0;
  store_key_.clear();
  sortby_.clear();
  get_patterns_.clear();
  ret_.clear();
}
MonitorCmd::MonitorCmd(const std::string& name, int arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly | kCmdFlagsAdmin, kAclCategoryAdmin) {}

bool MonitorCmd::DoInitial(PClient* client) { return true; }

void MonitorCmd::DoCmd(PClient* client) {
  client->AddToMonitor();
  client->SetRes(CmdRes::kOK);
}

}  // namespace pikiwidb
