/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include "cmd_admin.h"
#include "db.h"

#include "braft/raft.h"
#include "rocksdb/version.h"

#include "pikiwidb.h"
#include "praft/praft.h"
#include "pstd/env.h"

#include "cmd_table_manager.h"
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

  std::string db_path = g_config.db_path.ToString() + std::to_string(currentDBIndex);
  std::string path_temp = db_path;
  path_temp.append("_deleting/");
  pstd::RenameFile(db_path, path_temp);

  auto s = PSTORE.GetBackend(currentDBIndex)->Open();
  assert(s.ok());
  auto f = std::async(std::launch::async, [&path_temp]() { pstd::DeleteDir(path_temp); });
  PSTORE.GetBackend(currentDBIndex).get()->UnLock();
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
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsReadonly, kAclCategoryAdmin) {}

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

PingCmd::PingCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryList) {}

bool PingCmd::DoInitial(PClient* client) { return true; }

void PingCmd::DoCmd(PClient* client) { client->SetRes(CmdRes::kPong, "PONG"); }

const std::string InfoCmd::kInfoSection = "info";
const std::string InfoCmd::kAllSection = "all";
const std::string InfoCmd::kServerSection = "server";
const std::string InfoCmd::kClientsSection = "clients";
const std::string InfoCmd::kStatsSection = "stats";
const std::string InfoCmd::kCPUSection = "cpu";
const std::string InfoCmd::kReplicationSection = "replication";
const std::string InfoCmd::kKeyspaceSection = "keyspace";
const std::string InfoCmd::kDataSection = "data";
const std::string InfoCmd::kRocksDBSection = "rocksdb";
const std::string InfoCmd::kDebugSection = "debug";
const std::string InfoCmd::kCommandStatsSection = "commandstats";
const std::string InfoCmd::kCacheSection = "cache";
const std::string InfoCmd::kRaftSection = "RAFT";

InfoCmd::InfoCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsAdmin | kCmdFlagsReadonly, kAclCategoryAdmin) {}

bool InfoCmd::DoInitial(PClient* client) {
  size_t argc = client->argv_.size();
  if (argc > 4) {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }
  if (argc == 1) {
    info_section_ = kInfo;
    return true;
  }

  const auto& argv_ = client->argv_;
  if (strcasecmp(argv_[1].data(), kAllSection.data()) == 0) {
    info_section_ = kInfoAll;
  } else if (strcasecmp(argv_[1].data(), kServerSection.data()) == 0) {
    info_section_ = kInfoServer;
  } else if (strcasecmp(argv_[1].data(), kStatsSection.data()) == 0) {
    info_section_ = kInfoStats;
  } else if (strcasecmp(argv_[1].data(), kCPUSection.data()) == 0) {
    info_section_ = kInfoCPU;
  } else if (strcasecmp(argv_[1].data(), kDataSection.data()) == 0) {
    info_section_ = kInfoData;
  } else if (strcasecmp(argv_[1].data(), kRaftSection.data()) == 0) {
    info_section_ = kInfoRaft;
  } else if (strcasecmp(argv_[1].data(), kCommandStatsSection.data()) == 0) {
    info_section_ = kInfoCommandStats;
  }

  if (argc != 2) {
    client->SetRes(CmdRes::kSyntaxErr);
    return false;
  }
  return true;
}

// @todo The info raft command is only supported for the time being
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
      InfoRaft(info);
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
void InfoCmd::InfoRaft(std::string& message) {
  if (!PRAFT.IsInitialized()) {
    message += "-ERR:Don't already cluster member \r\n";
    return;
  }

  auto node_status = PRAFT.GetNodeStatus();
  if (node_status.state == braft::State::STATE_END) {
    message += "-ERR:Node is not initialized \r\n";
    return;
  }

  message += "raft_group_id:" + PRAFT.GetGroupID() + "\r\n";
  message += "raft_node_id:" + PRAFT.GetNodeID() + "\r\n";
  message += "raft_peer_id:" + PRAFT.GetPeerID() + "\r\n";
  if (braft::is_active_state(node_status.state)) {
    message += "raft_state:up\r\n";
  } else {
    message += "raft_state:down\r\n";
  }
  message += "raft_role:" + std::string(braft::state2str(node_status.state)) + "\r\n";
  message += "raft_leader_id:" + node_status.leader_id.to_string() + "\r\n";
  message += "raft_current_term:" + std::to_string(node_status.term) + "\r\n";

  if (PRAFT.IsLeader()) {
    std::vector<braft::PeerId> peers;
    auto status = PRAFT.GetListPeers(&peers);
    if (!status.ok()) {
      message += "-ERR:" + status.error_str();
      return;
    }

    for (int i = 0; i < peers.size(); i++) {
      message += "raft_node" + std::to_string(i) + ":addr=" + butil::ip2str(peers[i].addr.ip).c_str() +
                 ",port=" + std::to_string(peers[i].addr.port) + "\r\n";
    }
  }
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
  tmp_stream << "PikiwiDB_build_git_sha:" << KPIKIWIDB_GIT_COMMIT_ID << "\r\n";
  tmp_stream << "Pikiwidb_build_compile_date: " << KPIKIWIDB_BUILD_DATE << "\r\n";
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
  message += "# DATA \r\n ";
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
    if (iter.second.cmd_count != 0) {
      tmp_stream << iter.first << ":"
                 << "calls=" << iter.second.cmd_count
                 << ", usec=" << MethodofTotalTimeCalculation(iter.second.cmd_time_consuming) << ", usec_per_call=";
      if (!iter.second.cmd_time_consuming) {
        tmp_stream << 0 << "\r\n";
      } else {
        tmp_stream << MethodofCommandStatistics(iter.second.cmd_time_consuming, iter.second.cmd_count) << "\r\n";
      }
    }
  }
  info.append(tmp_stream.str());
}

}  // namespace pikiwidb
