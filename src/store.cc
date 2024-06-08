/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "store.h"

#include <memory>
#include <string>

#include "config.h"
#include "db.h"
#include "pstd/log.h"
#include "pstd/pstd_string.h"

namespace pikiwidb {

PStore::~PStore() { INFO("STORE is closing..."); }

PStore& PStore::Instance() {
  static PStore store;
  return store;
}

void PStore::Init(int db_number) {
  db_number_ = db_number;
  backends_.reserve(db_number_);
  for (int i = 0; i < db_number_; i++) {
    auto db = std::make_unique<DB>(i, g_config.db_path);
    db->Open();
    backends_.push_back(std::move(db));
    INFO("Open DB_{} success!", i);
  }
  INFO("STORE Init success!");
}

void PStore::HandleTaskSpecificDB(const TasksVector& tasks) {
  std::for_each(tasks.begin(), tasks.end(), [this](const auto& task) {
    if (task.db < 0 || task.db >= db_number_) {
      WARN("The database index is out of range.");
      return;
    }
    auto& db = backends_.at(task.db);
    switch (task.type) {
      case kCheckpoint: {
        if (auto s = task.args.find(kCheckpointPath); s == task.args.end()) {
          WARN("The critical parameter 'path' is missing for do a checkpoint.");
          return;
        }
        auto path = task.args.find(kCheckpointPath)->second;
        pstd::TrimSlash(path);
        db->CreateCheckpoint(path, task.sync);
        break;
      }
      case kLoadDBFromCheckpoint: {
        if (auto s = task.args.find(kCheckpointPath); s == task.args.end()) {
          WARN("The critical parameter 'path' is missing for load a checkpoint.");
          return;
        }
        auto path = task.args.find(kCheckpointPath)->second;
        pstd::TrimSlash(path);
        db->LoadDBFromCheckpoint(path, task.sync);
        break;
      }
      case kEmpty: {
        WARN("A empty task was passed in, not doing anything.");
        break;
      }
      default:
        break;
    }
  });
}
}  // namespace pikiwidb
