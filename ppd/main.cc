/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "brpc/server.h"
#include "butil/errno.h"
#include "gflags/gflags.h"
#include "spdlog/spdlog.h"

#include "pd_service.h"

DEFINE_int32(port, 8080, "Port of rpc server");
DEFINE_int32(idle_timeout_s, 60,
             "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s`");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

int main(int argc, char* argv[]) {
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
  brpc::Server server;
  PlacementDriverServiceImpl service;
  if (server.AddService(&service, brpc::SERVER_OWNS_SERVICE) != 0) {
    spdlog::error("Failed to add service for: {}", berror());
    return -1;
  }

  brpc::ServerOptions options;
  options.idle_timeout_sec = FLAGS_idle_timeout_s;
  options.max_concurrency = FLAGS_max_concurrency;

  // 启动服务
  if (server.Start(FLAGS_port, &options) != 0) {
    spdlog::error("Failed to start server for: {}", berror());
    return -1;
  }

  server.RunUntilAskedToQuit();
}