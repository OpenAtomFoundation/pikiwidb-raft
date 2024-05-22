/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  PikiwiDB.cc

#include "pikiwidb.h"

#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <thread>

#include "praft/praft.h"
#include "pstd/log.h"
#include "pstd/pstd_util.h"

#include "client.h"
#include "config.h"
#include "helper.h"
#include "pikiwidb_logo.h"
#include "slow_log.h"
#include "store.h"

std::unique_ptr<PikiwiDB> g_pikiwidb;
using namespace pikiwidb;

static void IntSigHandle(const int sig) {
  INFO("Catch Signal {}, cleanup...", sig);
  g_pikiwidb->Stop();
}

static void SignalSetup() {
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, &IntSigHandle);
  signal(SIGQUIT, &IntSigHandle);
  signal(SIGTERM, &IntSigHandle);
}

const uint32_t PikiwiDB::kRunidSize = 40;

static void Usage() {
  std::cerr << "Usage:  ./pikiwidb-server [/path/to/redis.conf] [options]\n\
        ./pikiwidb-server -v or --version\n\
        ./pikiwidb-server -h or --help\n\
Examples:\n\
        ./pikiwidb-server (run the server with default conf)\n\
        ./pikiwidb-server /etc/redis/6379.conf\n\
        ./pikiwidb-server --port 7777\n\
        ./pikiwidb-server --port 7777 --slaveof 127.0.0.1 8888\n\
        ./pikiwidb-server /etc/myredis.conf --loglevel verbose\n";
}

bool PikiwiDB::ParseArgs(int ac, char* av[]) {
  for (int i = 0; i < ac; i++) {
    if (cfg_file_.empty() && ::access(av[i], R_OK) == 0) {
      cfg_file_ = av[i];
      continue;
    } else if (strncasecmp(av[i], "-v", 2) == 0 || strncasecmp(av[i], "--version", 9) == 0) {
      std::cerr << "PikiwiDB Server version: " << KPIKIWIDB_VERSION << " bits=" << (sizeof(void*) == 8 ? 64 : 32)
                << std::endl;
      std::cerr << "PikiwiDB Server Build Type: " << KPIKIWIDB_BUILD_TYPE << std::endl;
#if defined(KPIKIWIDB_BUILD_DATE)
      std::cerr << "PikiwiDB Server Build Date: " << KPIKIWIDB_BUILD_DATE << std::endl;
#endif
#if defined(KPIKIWIDB_GIT_COMMIT_ID)
      std::cerr << "PikiwiDB Server Build GIT SHA: " << KPIKIWIDB_GIT_COMMIT_ID << std::endl;
#endif

      exit(0);
    } else if (strncasecmp(av[i], "-h", 2) == 0 || strncasecmp(av[i], "--help", 6) == 0) {
      Usage();
      exit(0);
    } else if (strncasecmp(av[i], "--port", 6) == 0) {
      if (++i == ac) {
        return false;
      }
      port_ = static_cast<uint16_t>(std::atoi(av[i]));
    } else if (strncasecmp(av[i], "--loglevel", 10) == 0) {
      if (++i == ac) {
        return false;
      }
      log_level_ = std::string(av[i]);
    } else if (strncasecmp(av[i], "--slaveof", 9) == 0) {
      if (i + 2 >= ac) {
        return false;
      }

      master_ = std::string(av[++i]);
      master_port_ = static_cast<int16_t>(std::atoi(av[++i]));
    } else {
      std::cerr << "Unknow option " << av[i] << std::endl;
      return false;
    }
  }

  return true;
}

void PikiwiDB::OnNewConnection(pikiwidb::TcpConnection* obj) {
  INFO("New connection from {}:{}", obj->GetPeerIP(), obj->GetPeerPort());

  auto client = std::make_shared<pikiwidb::PClient>(obj);
  obj->SetContext(client);

  client->OnConnect();

  auto msg_cb = std::bind(&pikiwidb::PClient::HandlePackets, client.get(), std::placeholders::_1, std::placeholders::_2,
                          std::placeholders::_3);
  obj->SetMessageCallback(msg_cb);
  obj->SetOnDisconnect([](pikiwidb::TcpConnection* obj) {
    INFO("disconnect from {}", obj->GetPeerIP());
    obj->GetContext<pikiwidb::PClient>()->SetState(pikiwidb::ClientState::kClosed);
  });
  obj->SetNodelay(true);
  obj->SetEventLoopSelector([this]() { return worker_threads_.ChooseNextWorkerEventLoop(); });
  obj->SetSlaveEventLoopSelector([this]() { return slave_threads_.ChooseNextWorkerEventLoop(); });
}

bool PikiwiDB::Init() {
  char runid[kRunidSize + 1] = "";
  getRandomHexChars(runid, kRunidSize);
  g_config.Set("runid", {runid, kRunidSize}, true);

  if (port_ != 0) {
    g_config.Set("port", std::to_string(port_), true);
  }

  if (!log_level_.empty()) {
    g_config.Set("log-level", log_level_, true);
  }

  NewTcpConnectionCallback cb = std::bind(&PikiwiDB::OnNewConnection, this, std::placeholders::_1);
  if (!worker_threads_.Init(g_config.ip.ToString().c_str(), g_config.port.load(), cb)) {
    ERROR("worker_threads Init failed. IP = {} Port = {}", g_config.ip.ToString(), g_config.port.load());
    return false;
  }

  auto num = g_config.worker_threads_num.load() + g_config.slave_threads_num.load();
  auto kMaxWorkerNum = IOThreadPool::GetMaxWorkerNum();
  if (num > kMaxWorkerNum) {
    ERROR("number of threads can't exceeds {}, now is {}", kMaxWorkerNum, num);
    return false;
  }
  worker_threads_.SetWorkerNum(static_cast<size_t>(g_config.worker_threads_num.load()));
  slave_threads_.SetWorkerNum(static_cast<size_t>(g_config.slave_threads_num.load()));

  // now we only use fast cmd thread pool
  auto status = cmd_threads_.Init(g_config.fast_cmd_threads_num.load(), 0, "pikiwidb-cmd");
  if (!status.ok()) {
    ERROR("init cmd thread pool failed: {}", status.ToString());
    return false;
  }

  PSTORE.Init(g_config.databases.load(std::memory_order_relaxed));

  PSlowLog::Instance().SetThreshold(g_config.slow_log_time.load());
  PSlowLog::Instance().SetLogLimit(static_cast<std::size_t>(g_config.slow_log_max_len.load()));

  // init base loop
  auto loop = worker_threads_.BaseLoop();
  loop->ScheduleRepeatedly(1000, &PReplication::Cron, &PREPL);

  // master ip
  if (!g_config.ip.empty()) {
    PREPL.SetMasterAddr(g_config.master_ip.ToString().c_str(), g_config.master_port.load());
  }

  //  cmd_table_manager_.InitCmdTable();

  return true;
}

void PikiwiDB::Run() {
  worker_threads_.SetName("pikiwi-main");
  slave_threads_.SetName("pikiwi-slave");

  cmd_threads_.Start();

  std::thread t([this]() {
    auto slave_loop = slave_threads_.BaseLoop();
    slave_loop->Init();
    slave_threads_.Run(0, nullptr);
  });

  worker_threads_.Run(0, nullptr);

  if (t.joinable()) {
    t.join();  // wait for slave thread exit
  }
  INFO("server exit running");
}

void PikiwiDB::Stop() {
  pikiwidb::PRAFT.ShutDown();
  pikiwidb::PRAFT.Join();
  pikiwidb::PRAFT.Clear();
  slave_threads_.Exit();
  worker_threads_.Exit();
  cmd_threads_.Stop();
}

// pikiwidb::CmdTableManager& PikiwiDB::GetCmdTableManager() { return cmd_table_manager_; }

static void InitLogs() {
  logger::Init("logs/pikiwidb_server.log");

#if BUILD_DEBUG
  spdlog::set_level(spdlog::level::debug);
#else
  spdlog::set_level(spdlog::level::info);
#endif
}

static void daemonize() {
  if (fork()) {
    exit(0); /* parent exits */
  }
  setsid(); /* create a new session */
}

static void closeStd() {
  int fd;
  fd = open("/dev/null", O_RDWR, 0);
  if (fd != -1) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
  }
}

int main(int ac, char* av[]) {
  g_pikiwidb = std::make_unique<PikiwiDB>();
  if (!g_pikiwidb->ParseArgs(ac - 1, av + 1)) {
    Usage();
    return -1;
  }

  if (!g_pikiwidb->GetConfigName().empty()) {
    if (!g_config.LoadFromFile(g_pikiwidb->GetConfigName())) {
      std::cerr << "Load config file [" << g_pikiwidb->GetConfigName() << "] failed!\n";
      return -1;
    }
  }

  // output logo to console
  char logo[512] = "";
  snprintf(logo, sizeof logo - 1, pikiwidbLogo, KPIKIWIDB_VERSION, static_cast<int>(sizeof(void*)) * 8,
           static_cast<int>(g_config.port));
  std::cout << logo;

  if (g_config.daemonize.load()) {
    daemonize();
  }

  pstd::InitRandom();
  SignalSetup();
  InitLogs();

  if (g_config.daemonize.load()) {
    closeStd();
  }

  if (g_pikiwidb->Init()) {
    g_pikiwidb->Run();
  }

  // when process exit, flush log
  spdlog::get(logger::Logger::Instance().Name())->flush();
  return 0;
}
