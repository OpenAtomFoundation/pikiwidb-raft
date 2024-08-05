/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_table_manager.h"
#include "cmd_thread_pool.h"
#include "common.h"
#include "net/event_server.h"

#define KPIKIWIDB_VERSION "4.0.0"

#ifdef BUILD_DEBUG
#  define KPIKIWIDB_BUILD_TYPE "DEBUG"
#else
#  define KPIKIWIDB_BUILD_TYPE "RELEASE"
#endif

namespace pikiwidb {
class PRaft;
}  // namespace pikiwidb

class PikiwiDB final {
 public:
  PikiwiDB() = default;
  ~PikiwiDB() = default;

  bool ParseArgs(int ac, char* av[]);
  const PString& GetConfigName() const { return cfg_file_; }

  bool Init();
  void Run();
  //  void Recycle();
  void Stop();

  static void OnNewConnection(uint64_t connId, std::shared_ptr<pikiwidb::PClient>& client, const net::SocketAddr& addr);

  //  pikiwidb::CmdTableManager& GetCmdTableManager();
  uint32_t GetCmdID() { return ++cmd_id_; };

  void SubmitFast(const std::shared_ptr<pikiwidb::CmdThreadPoolTask>& runner) { cmd_threads_.SubmitFast(runner); }
  void SubmitSlow(const std::shared_ptr<pikiwidb::CmdThreadPoolTask>& runner) { cmd_threads_.SubmitSlow(runner); }

  void PushWriteTask(const std::shared_ptr<pikiwidb::PClient>& client) {
    std::string msg;
    client->Message(&msg);
    client->SendOver();
    event_server_->SendPacket(client, std::move(msg));
  }

  inline void SendPacket2Client(const std::shared_ptr<pikiwidb::PClient>& client, std::string&& msg) {
    event_server_->SendPacket(client, std::move(msg));
  }

  inline void CloseConnection(const std::shared_ptr<pikiwidb::PClient>& client) {
    event_server_->CloseConnection(client);
  }

  void TCPConnect(
      const net::SocketAddr& addr,
      const std::function<void(uint64_t, std::shared_ptr<pikiwidb::PClient>&, const net::SocketAddr&)>& onConnect,
      const std::function<void(std::string)>& cb);

 public:
  PString cfg_file_;
  uint16_t port_{0};
  PString log_level_;

  PString master_;
  uint16_t master_port_{0};

  static const uint32_t kRunidSize;

 private:
  pikiwidb::CmdThreadPool cmd_threads_;

  std::unique_ptr<net::EventServer<std::shared_ptr<pikiwidb::PClient>>> event_server_;
  uint32_t cmd_id_ = 0;
};

extern std::unique_ptr<PikiwiDB> g_pikiwidb;
