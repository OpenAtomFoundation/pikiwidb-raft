// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Responsible for managing the runtime configuration information of PikiwiDB.
 */

#include <string>
#include <system_error>
#include <vector>

#include "config.h"
#include "pstd/pstd_string.h"
#include "store.h"

namespace pikiwidb {

constexpr uint16_t PORT_LIMIT_MAX = 65535;
constexpr uint16_t PORT_LIMIT_MIN = 1;
constexpr int DBNUMBER_MAX = 16;
constexpr int THREAD_MAX = 129;
constexpr int ROCKSDB_INSTANCE_NUMBER_MAX = 10;

PConfig g_config;

// preprocess func
static void EraseQuotes(std::string& str) {
  // convert "hello" to  hello
  if (str.size() < 2) {
    return;
  }
  if (str[0] == '"' && str[str.size() - 1] == '"') {
    str.erase(str.begin());
    str.pop_back();
  }
}

static Status CheckYesNo(const std::string& value) {
  if (!pstd::StringEqualCaseInsensitive(value, "yes") && !pstd::StringEqualCaseInsensitive(value, "no")) {
    return Status::InvalidArgument("The value must be yes or no.");
  }
  return Status::OK();
}

static Status CheckLogLevel(const std::string& value) {
  if (!pstd::StringEqualCaseInsensitive(value, "debug") && !pstd::StringEqualCaseInsensitive(value, "verbose") &&
      !pstd::StringEqualCaseInsensitive(value, "notice") && !pstd::StringEqualCaseInsensitive(value, "warning")) {
    return Status::InvalidArgument("The value must be debug / verbose / notice / warning.");
  }
  return Status::OK();
}

Status BaseValue::Set(const std::string& value, bool init_stage) {
  if (!init_stage && !rewritable_) {
    return Status::NotSupported("Dynamic modification is not supported.");
  }
  auto value_copy = value;
  EraseQuotes(value_copy);
  auto s = check(value_copy);
  if (!s.ok()) {
    return s;
  }
  // TODO(dingxiaoshuai) Support RocksDB config change Dynamically
  return SetValue(value_copy);
}

Status StringValue::SetValue(const std::string& value) {
  auto values = SplitString(value, delimiter_);
  if (values.size() != values_.size()) {
    return Status::InvalidArgument("The number of parameters does not match.");
  }
  for (int i = 0; i < values_.size(); i++) {
    *values_[i] = std::move(values[i]);
  }
  return Status::OK();
}

Status BoolValue::SetValue(const std::string& value) {
  if (pstd::StringEqualCaseInsensitive(value, "yes")) {
    value_->store(true);
  } else {
    value_->store(false);
  }
  return Status::OK();
}

template <typename T>
Status NumberValue<T>::SetValue(const std::string& value) {
  T v;
  auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.length(), v);
  if (ec != std::errc()) {
    return Status::InvalidArgument("Failed to convert to a number.");
  }
  if (v < value_min_) {
    v = value_min_;
  }
  if (v > value_max_) {
    v = value_max_;
  }
  value_->store(v);
  return Status::OK();
}

PConfig::PConfig() {
  AddBool("daemonize", &CheckYesNo, false, &daemonize);
  AddString("ip", false, {&ip});
  AddNumberWithLimit<uint16_t>("port", false, &port, PORT_LIMIT_MIN, PORT_LIMIT_MAX);
  AddNumber("raft-port-offset", true, &raft_port_offset);
  AddNumber("timeout", true, &timeout);
  AddString("db-path", false, {&db_path});
  AddStringWithFunc("loglevel", &CheckLogLevel, false, {&log_level});
  AddString("logfile", false, {&log_dir});
  AddNumberWithLimit<size_t>("databases", false, &databases, 1, DBNUMBER_MAX);
  AddString("requirepass", true, {&password});
  AddNumber("maxclients", true, &max_clients);
  AddNumberWithLimit<uint32_t>("worker-threads", false, &worker_threads_num, 1, THREAD_MAX);
  AddNumberWithLimit<uint32_t>("slave-threads", false, &worker_threads_num, 1, THREAD_MAX);
  AddNumber("slowlog-log-slower-than", true, &slow_log_time);
  AddNumber("slowlog-max-len", true, &slow_log_max_len);
  AddNumberWithLimit<size_t>("db-instance-num", true, &db_instance_num, 1, ROCKSDB_INSTANCE_NUMBER_MAX);
  AddNumberWithLimit<int32_t>("fast-cmd-threads-num", false, &fast_cmd_threads_num, 1, THREAD_MAX);
  AddNumberWithLimit<int32_t>("slow-cmd-threads-num", false, &slow_cmd_threads_num, 1, THREAD_MAX);
  AddNumber("max-client-response-size", true, &max_client_response_size);
  AddString("runid", false, {&run_id});
  AddNumber("small-compaction-threshold", true, &small_compaction_threshold);
  AddNumber("small-compaction-duration-threshold", true, &small_compaction_duration_threshold);
  AddBool("use-raft", &CheckYesNo, false, &use_raft);

  // rocksdb config
  AddNumber("rocksdb-max-subcompactions", false, &rocksdb_max_subcompactions);
  AddNumber("rocksdb-max-background-jobs", false, &rocksdb_max_background_jobs);
  AddNumber("rocksdb-max-write-buffer-number", false, &rocksdb_max_write_buffer_number);
  AddNumber("rocksdb-min-write-buffer-number-to-merge", false, &rocksdb_min_write_buffer_number_to_merge);
  AddNumber("rocksdb-write-buffer-size", false, &rocksdb_write_buffer_size);
  AddNumber("rocksdb-level0-file-num-compaction-trigger", false, &rocksdb_level0_file_num_compaction_trigger);
  AddNumber("rocksdb-number-levels", true, &rocksdb_num_levels);
  AddBool("rocksdb-enable-pipelined-write", CheckYesNo, false, &rocksdb_enable_pipelined_write);
  AddNumber("rocksdb-level0-slowdown-writes-trigger", false, &rocksdb_level0_slowdown_writes_trigger);
  AddNumber("rocksdb-level0-stop-writes-trigger", false, &rocksdb_level0_stop_writes_trigger);
  AddNumber("rocksdb-level0-slowdown-writes-trigger", false, &rocksdb_level0_slowdown_writes_trigger);

  // cache config
  AddNumberWithLimit("cache-num", true, &cache_num, 1, 48);
  AddNumberWithLimit("cache-mode", true, &cache_mode, 0, 1);
  AddNumber("zset-cache-field-num-per-key", true, &zset_cache_field_num_per_key);
  AddNumber("zset-cache-start-direction", true, &zset_cache_start_direction);
  AddNumber("cache-maxmemory", true, &cache_maxmemory);
  AddNumber("cache-maxmemory-policy", true, &cache_maxmemory_policy);
  AddNumber("cache-maxmemory-samples", true, &cache_maxmemory_samples);
  AddNumber("cache-lfu-decay-time", true, &cache_lfu_decay_time);
}

bool PConfig::LoadFromFile(const std::string& file_name) {
  config_file_name_ = file_name;
  if (!parser_.Load(file_name.c_str())) {
    return false;
  }

  // During the initialization phase, so there is no need to hold a lock.
  for (auto& [key, value] : parser_.GetMap()) {
    if (auto iter = config_map_.find(key); iter != config_map_.end()) {
      auto& v = config_map_[key];
      auto s = v->Set(value.at(0), true);
      if (!s.ok()) {
        return false;
      }
    }
  }

  // Handle separately
  std::vector<PString> master(SplitString(parser_.GetData<PString>("slaveof"), ' '));
  if (master.size() == 2) {
    master_ip = std::move(master[0]);
    master_port = static_cast<uint16_t>(std::stoi(master[1]));
  }

  std::vector<PString> alias(SplitString(parser_.GetData<PString>("rename-command"), ' '));
  if (alias.size() % 2 == 0) {
    for (auto it(alias.begin()); it != alias.end();) {
      const PString& oldCmd = *(it++);
      const PString& newCmd = *(it++);
      aliases[oldCmd] = newCmd;
    }
  }

  std::string all_cache_type_str;
  all_cache_type_str = parser_.GetData<PString>("cache-type");
  SetCacheType(all_cache_type_str);

  return true;
}

void PConfig::SetCacheType(const std::string& value) {
  cache_string = cache_set = cache_zset = cache_hash = cache_list = cache_bit = 0;
  if (value == "") {
    return;
  }

  std::string lower_value = value;
  pstd::StringToLower(lower_value);
  lower_value.erase(remove_if(lower_value.begin(), lower_value.end(), isspace), lower_value.end());
  pstd::StringSplit(lower_value, ',', cache_type_all);
  for (auto& type : cache_type_all) {
    if (type == "string") {
      cache_string = 1;
    } else if (type == "set") {
      cache_set = 1;
    } else if (type == "zset") {
      cache_zset = 1;
    } else if (type == "hash") {
      cache_hash = 1;
    } else if (type == "list") {
      cache_list = 1;
    } else if (type == "bit") {
      cache_bit = 1;
    }
  }
}

void PConfig::Get(const std::string& key, std::vector<std::string>* values) const {
  values->clear();
  for (const auto& [k, v] : config_map_) {
    if (key == "*" || pstd::StringMatch(key.c_str(), k.c_str(), 1)) {
      values->emplace_back(k);
      values->emplace_back(v->Value());
    }
  }
}

Status PConfig::Set(std::string key, const std::string& value, bool init_stage) {
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);
  auto iter = config_map_.find(key);
  if (iter == config_map_.end()) {
    return Status::NotFound("Non-existent configuration items.");
  }
  return iter->second->Set(value, init_stage);
}

rocksdb::Options PConfig::GetRocksDBOptions() {
  rocksdb::Options options;
  options.create_if_missing = true;
  options.create_missing_column_families = true;
  options.max_subcompactions = rocksdb_max_subcompactions;
  options.max_background_jobs = rocksdb_max_background_jobs;
  options.max_write_buffer_number = rocksdb_max_write_buffer_number;
  options.min_write_buffer_number_to_merge = rocksdb_min_write_buffer_number_to_merge;
  options.write_buffer_size = rocksdb_write_buffer_size;
  options.level0_file_num_compaction_trigger = rocksdb_level0_file_num_compaction_trigger;
  options.num_levels = rocksdb_num_levels;
  options.enable_pipelined_write = rocksdb_enable_pipelined_write;
  options.level0_slowdown_writes_trigger = rocksdb_level0_slowdown_writes_trigger;
  options.level0_stop_writes_trigger = rocksdb_level0_stop_writes_trigger;
  return options;
}

rocksdb::BlockBasedTableOptions PConfig::GetRocksDBBlockBasedTableOptions() {
  rocksdb::BlockBasedTableOptions options;
  return options;
}

}  // namespace pikiwidb
