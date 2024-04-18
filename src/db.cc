/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "db.h"
#include "config.h"

extern pikiwidb::PConfig g_config;

namespace pikiwidb {

DB::DB(int db_id, const std::string &db_path) : db_id_(db_id), db_path_(db_path + std::to_string(db_id) + '/') {
  storage::StorageOptions storage_options;
  storage_options.options = g_config.GetRocksDBOptions();
  // some options obj for all RocksDB in one DB.
  auto cap = storage_options.db_instance_num * kColumnNum * storage_options.options.write_buffer_size *
             storage_options.options.max_write_buffer_number;
  storage_options.options.write_buffer_manager = std::make_shared<rocksdb::WriteBufferManager>(cap);

  storage_options.table_options = g_config.GetRocksDBBlockBasedTableOptions();

  storage_options.small_compaction_threshold = g_config.small_compaction_threshold.load();
  storage_options.small_compaction_duration_threshold = g_config.small_compaction_duration_threshold.load();
  storage_options.db_instance_num = g_config.db_instance_num;
  storage_options.db_id = db_id;

  storage_ = std::make_unique<storage::Storage>();
  if (auto s = storage_->Open(storage_options, db_path_); !s.ok()) {
    ERROR("Storage open failed! {}", s.ToString());
    abort();
  }
  opened_ = true;
}

}  // namespace pikiwidb
