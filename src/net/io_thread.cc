/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "io_thread.h"

namespace net {

void IOThread::Stop() {
  bool run = true;
  if (running_.compare_exchange_strong(run, false)) {
    baseEvent_->Close();
    Wait();
  }
}

void IOThread::Wait() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool IOThread::Run() {
  if (!baseEvent_->Init()) {
    return false;
  }

  thread_ = std::thread([this] { baseEvent_->EventPoll(); });
  return true;
}

}  // namespace net
