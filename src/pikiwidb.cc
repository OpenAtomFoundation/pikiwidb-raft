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
#include <sys/resource.h>
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

void PikiwiDB::OnNewConnection(uint64_t connId, std::shared_ptr<pikiwidb::PClient>& client,
                               const net::SocketAddr& addr) {
  INFO("New connection from {}:{}", addr.GetIP(), addr.GetPort());
  client->SetSocketAddr(addr);
  client->OnConnect();
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

  auto num = g_config.worker_threads_num.load() + g_config.slave_threads_num.load();

  // now we only use fast cmd thread pool
  auto status = cmd_threads_.Init(g_config.fast_cmd_threads_num.load(), 0, "pikiwidb-cmd");
  if (!status.ok()) {
    ERROR("init cmd thread pool failed: {}", status.ToString());
    return false;
  }

  PSTORE.Init(g_config.databases.load(std::memory_order_relaxed));

  PSlowLog::Instance().SetThreshold(g_config.slow_log_time.load());
  PSlowLog::Instance().SetLogLimit(static_cast<std::size_t>(g_config.slow_log_max_len.load()));

  // master ip
  if (!g_config.master_ip.empty()) {
    PREPL.SetMasterAddr(g_config.master_ip.ToString().c_str(), g_config.master_port.load());
  }

  event_server_ = std::make_unique<net::EventServer<std::shared_ptr<PClient>>>(num);

  event_server_->SetRwSeparation(true);

  net::SocketAddr addr(g_config.ip.ToString(), g_config.port.load());
  INFO("Add listen addr:{}, port:{}", g_config.ip.ToString(), g_config.port.load());
  event_server_->AddListenAddr(addr);

  event_server_->SetOnInit([](std::shared_ptr<PClient>* client) { *client = std::make_shared<PClient>(); });

  event_server_->SetOnCreate([](uint64_t connID, std::shared_ptr<PClient>& client, const net::SocketAddr& addr) {
    client->SetSocketAddr(addr);
    client->OnConnect();
    INFO("New connection from fd:{} IP:{} port:{}", connID, addr.GetIP(), addr.GetPort());
  });

  event_server_->SetOnMessage([](std::string&& msg, std::shared_ptr<PClient>& t) {
    t->handlePacket(msg.c_str(), static_cast<int>(msg.size()));
  });

  event_server_->SetOnClose([](std::shared_ptr<PClient>& client, std::string&& msg) {
    INFO("Close connection id:{} msg:{}", client->GetConnId(), msg);
    client->OnClose();
  });

  event_server_->InitTimer(10);

  auto timerTask = std::make_shared<net::CommonTimerTask>(1000);
  timerTask->SetCallback([]() { PREPL.Cron(); });
  event_server_->AddTimerTask(timerTask);

  return true;
}

void PikiwiDB::Run() {
  auto [ret, err] = event_server_->StartServer();
  if (!ret) {
    ERROR("start server failed: {}", err);
    return;
  }

  cmd_threads_.Start();
  event_server_->Wait();
  INFO("server exit running");
}

void PikiwiDB::Stop() {
  pikiwidb::PRAFT.ShutDown();
  pikiwidb::PRAFT.Join();
  pikiwidb::PRAFT.Clear();
  cmd_threads_.Stop();
  event_server_->StopServer();
}

void PikiwiDB::TCPConnect(
    const net::SocketAddr& addr,
    const std::function<void(uint64_t, std::shared_ptr<pikiwidb::PClient>&, const net::SocketAddr&)>& onConnect,
    const std::function<void(std::string)>& cb) {
  INFO("Connect to {}:{}", addr.GetIP(), addr.GetPort());
  event_server_->TCPConnect(addr, onConnect, cb);
}

static void InitLogs() {
  logger::Init("logs/pikiwidb_server.log");

#if BUILD_DEBUG
  spdlog::set_level(spdlog::level::debug);
#else
  spdlog::set_level(spdlog::level::info);
#endif
}

static int InitLimit() {
  rlimit limit;
  rlim_t maxfiles = g_config.max_clients;
  if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
    WARN("getrlimit error: {}", strerror(errno));
  } else if (limit.rlim_cur < maxfiles) {
    rlim_t old_limit = limit.rlim_cur;
    limit.rlim_cur = maxfiles;
    limit.rlim_max = maxfiles;
    if (setrlimit(RLIMIT_NOFILE, &limit) != -1) {
      WARN("your 'limit -n ' of {} is not enough for PikiwiDB to start. PikiwiDB have successfully reconfig it to ",
           old_limit, limit.rlim_cur);
    } else {
      ERROR(
          "your 'limit -n ' of {} is not enough for PikiwiDB to start."
          " PikiwiDB can not reconfig it({}), do it by yourself",
          old_limit, strerror(errno));
      return -1;
    }
  }
  return 0;
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

  InitLimit();
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
