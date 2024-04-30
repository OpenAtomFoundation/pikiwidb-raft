/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "db.h"

#include "config.h"
#include "praft/praft.h"
#include "pstd/log.h"

extern pikiwidb::PConfig g_config;

namespace pikiwidb {

DB::DB(int db_index, const std::string& db_path, int rocksdb_inst_num)
    : db_index_(db_index), db_path_(db_path + std::to_string(db_index_) + '/'), rocksdb_inst_num_(rocksdb_inst_num) {
  storage::StorageOptions storage_options;
  storage_options.options = g_config.GetRocksDBOptions();
  storage_options.db_instance_num = rocksdb_inst_num_;
  storage_options.db_id = db_index_;

  // options for CF
  storage_options.options.ttl = g_config.rocksdb_ttl_second.load(std::memory_order_relaxed);
  storage_options.options.periodic_compaction_seconds = g_config.rocksdb_periodic_second.load(std::memory_order_relaxed);
  if (g_config.use_raft.load(std::memory_order_relaxed)) {
    storage_options.append_log_function = [&r = PRAFT](const Binlog& log, std::promise<rocksdb::Status>&& promise) {
      r.AppendLog(log, std::move(promise));
    };
    storage_options.do_snapshot_function =
        std::bind(&pikiwidb::PRaft::DoSnapshot, &pikiwidb::PRAFT, std::placeholders::_1, std::placeholders::_2);
  }
  storage_ = std::make_unique<storage::Storage>();

  if (auto s = storage_->Open(storage_options, db_path_); !s.ok()) {
    ERROR("Storage open failed! {}", s.ToString());
    abort();
  }

  opened_.store(true);
  INFO("Open DB{} success!", db_index_);
}

void DB::DoCheckpoint(const std::string& path, int i) {
  // 1) always hold the storage's shared lock
  std::shared_lock sharedLock(storage_mutex_);

  // 2）Create the checkpoint of rocksdb i.
  auto status = storage_->CreateCheckpoint(path, i);
}

void DB::LoadCheckpoint(const std::string& path, const std::string& db_path, int i) {
  // 1) Already holding the storage's exclusion lock

  // 2) Load the checkpoint of rocksdb i.
  auto status = storage_->LoadCheckpoint(path, db_path, i);
}

void DB::CreateCheckpoint(const std::string& path, bool sync) {
  auto tmp_path = path + '/' + std::to_string(db_index_);
  if (0 != pstd::CreatePath(tmp_path)) {
    WARN("Create dir {} fail !", tmp_path);
    return;
  }

  std::vector<std::future<void>> result;
  result.reserve(rocksdb_inst_num_);
  for (int i = 0; i < rocksdb_inst_num_; ++i) {
    // In a new thread, create a checkpoint for the specified rocksdb i
    // In DB::DoBgSave, a read lock is always held to protect the Storage
    // corresponding to this rocksdb i.
    auto res = std::async(std::launch::async, &DB::DoCheckpoint, this, path, i);
    result.push_back(std::move(res));
  }
  if (sync) {
    for (auto& r : result) {
      r.get();
    }
  }
}

void DB::LoadDBFromCheckpoint(const std::string& path, bool sync) {
  opened_.store(false);
  auto checkpoint_path = path + '/' + std::to_string(db_index_);
  if (0 != pstd::IsDir(path)) {
    WARN("Checkpoint dir {} does not exist!", checkpoint_path);
    return;
  }
  if (0 != pstd::IsDir(db_path_)) {
    if (0 != pstd::CreateDir(db_path_)) {
      WARN("Create dir {} fail !", db_path_);
      return;
    }
  }

  std::lock_guard<std::shared_mutex> lock(storage_mutex_);
  std::vector<std::future<void>> result;
  result.reserve(rocksdb_inst_num_);
  for (int i = 0; i < rocksdb_inst_num_; ++i) {
    // In a new thread, Load a checkpoint for the specified rocksdb i
    auto res = std::async(std::launch::async, &DB::LoadCheckpoint, this, checkpoint_path, db_path_, i);
    result.push_back(std::move(res));
  }
  for (auto& r : result) {
    r.get();
  }

  storage::StorageOptions storage_options;
  storage_options.options.create_if_missing = true;
  storage_options.db_instance_num = rocksdb_inst_num_;
  storage_options.db_id = db_index_;

  // options for CF
  storage_options.options.ttl = g_config.rocksdb_ttl_second.load(std::memory_order_relaxed);
  storage_options.options.periodic_compaction_seconds = g_config.rocksdb_periodic_second.load(std::memory_order_relaxed);
  if (g_config.use_raft.load(std::memory_order_relaxed)) {
    storage_options.append_log_function = [&r = PRAFT](const Binlog& log, std::promise<rocksdb::Status>&& promise) {
      r.AppendLog(log, std::move(promise));
    };
  }
  storage_ = std::make_unique<storage::Storage>();

  if (auto s = storage_->Open(storage_options, db_path_); !s.ok()) {
    ERROR("Storage open failed! {}", s.ToString());
    abort();
  }
  opened_.store(true);
  INFO("DB{} load a checkpoint from {} success!", db_index_, path);
}
}  // namespace pikiwidb
