/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "proxy_service.h"

#include <array>
#include <memory>
#include <string>

namespace pikiwidb::proxy {
void ProxyServiceImpl::RunCommand(::google::protobuf::RpcController* cntl,
                                  const pikiwidb::proxy::RunCommandRequest* request,
                                  pikiwidb::proxy::RunCommandResponse* response, ::google::protobuf::Closure* done) {
  std::string command = request->command();  // 检查命令是否在白名单中

  if (!IsCommandAllowed(command)) {
    response->set_error("Command not allowed");
    done->Run();
    return;
  }

  std::string output = ExecuteCommand(command);
  if (output.empty()) {
    response->set_error("Command execution failed");
  } else {
    response->set_output(output);
  }
  done->Run();
}

void ProxyServiceImpl::GetRouteINfo(::google::protobuf::RpcController* cntl,
                                    const pikiwidb::proxy::GetRouteInfoRequest* request,
                                    pikiwidb::proxy::GetRouteInfoResponse* response,
                                    ::google::protobuf::Closure* done) {}

std::string ProxyServiceImpl::ExecuteCommand(const std::string& command) {
  if (!IsCommandAllowed(command)) {
    return "Command not allowed";
  }

  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
  if (!pipe) {
    return "Failed to execute command";
  }

  while (true) {
    if (fgets(buffer.data(), buffer.size(), pipe.get()) == nullptr) {
      if (feof(pipe.get())) {
        break;
      } else {
        return "Error reading command output";
      }
    }
    result += buffer.data();
  }
  return result;
}

}  // namespace pikiwidb::proxy