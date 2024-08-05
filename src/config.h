/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rocksdb/options.h"
#include "rocksdb/table.h"

#include "common.h"
#include "config_parser.h"

namespace pikiwidb {

using Status = rocksdb::Status;
using CheckFunc = std::function<Status(const std::string&)>;
class PConfig;

extern PConfig g_config;

class BaseValue {
 public:
  BaseValue(const std::string& key, CheckFunc check_func_ptr, bool rewritable = false)
      : key_(key), custom_check_func_ptr_(check_func_ptr), rewritable_(rewritable) {}

  virtual ~BaseValue() = default;

  const std::string& Key() const { return key_; }

  virtual std::string Value() const = 0;

  Status Set(const std::string& value, bool force);

 protected:
  virtual Status SetValue(const std::string&) = 0;
  Status check(const std::string& value) {
    if (!custom_check_func_ptr_) {
      return Status::OK();
    }
    return custom_check_func_ptr_(value);
  }

 protected:
  std::string key_;
  CheckFunc custom_check_func_ptr_ = nullptr;
  bool rewritable_ = false;
};

class StringValue : public BaseValue {
 public:
  StringValue(const std::string& key, CheckFunc check_func_ptr, bool rewritable,
              const std::vector<AtomicString*>& value_ptr_vec, char delimiter = ' ')
      : BaseValue(key, check_func_ptr, rewritable), values_(value_ptr_vec), delimiter_(delimiter) {
    assert(!values_.empty());
  }
  ~StringValue() override = default;

  std::string Value() const override { return MergeString(values_, delimiter_); };

 private:
  Status SetValue(const std::string& value) override;

  std::vector<AtomicString*> values_;
  char delimiter_ = 0;
};

template <typename T>
class NumberValue : public BaseValue {
 public:
  NumberValue(const std::string& key, CheckFunc check_func_ptr, bool rewritable, std::atomic<T>* value_ptr,
              T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max())
      : BaseValue(key, check_func_ptr, rewritable), value_(value_ptr), value_min_(min), value_max_(max) {
    assert(value_ != nullptr);
    assert(value_min_ <= value_max_);
  };

  std::string Value() const override { return std::to_string(value_->load()); }

 private:
  Status SetValue(const std::string& value) override;

  std::atomic<T>* value_ = nullptr;
  T value_min_;
  T value_max_;
};

class BoolValue : public BaseValue {
 public:
  BoolValue(const std::string& key, CheckFunc check_func_ptr, bool rewritable, std::atomic<bool>* value_ptr)
      : BaseValue(key, check_func_ptr, rewritable), value_(value_ptr) {
    assert(value_ != nullptr);
  };

  std::string Value() const override { return value_->load() ? "yes" : "no"; };

 private:
  Status SetValue(const std::string& value) override;
  std::atomic<bool>* value_ = nullptr;
};

using ValuePrt = std::unique_ptr<BaseValue>;
using ConfigMap = std::unordered_map<std::string, ValuePrt>;

class PConfig {
 public:
  PConfig();
  ~PConfig() = default;
  bool LoadFromFile(const std::string& file_name);
  const std::string& ConfigFileName() const { return config_file_name_; }
  void Get(const std::string&, std::vector<std::string>*) const;
  Status Set(std::string, const std::string&, bool force = false);

 public:
  std::atomic_uint32_t timeout = 0;
  // auth
  AtomicString password;
  AtomicString master_auth;
  AtomicString master_ip;
  std::map<std::string, std::string> aliases;
  std::atomic_uint32_t max_clients = 10000;     // 10000
  std::atomic_uint32_t slow_log_time = 1000;    // 1000 microseconds
  std::atomic_uint32_t slow_log_max_len = 128;  // 128
  std::atomic_uint32_t master_port;             // replication
  AtomicString include_file;                    // the template config
  std::vector<PString> modules;                 // modules
  std::atomic_int32_t fast_cmd_threads_num = 4;
  std::atomic_int32_t slow_cmd_threads_num = 4;
  std::atomic_uint64_t max_client_response_size = 1073741824;
  std::atomic_uint64_t small_compaction_threshold = 604800;
  std::atomic_uint64_t small_compaction_duration_threshold = 259200;

  std::atomic_bool daemonize = false;
  AtomicString pid_file = "./pikiwidb.pid";
  AtomicString ip = "127.0.0.1";
  std::atomic_uint16_t port = 9221;
  std::atomic_uint16_t raft_port_offset = 10;
  AtomicString db_path = "./db/";
  AtomicString log_dir = "stdout";  // the log directory, differ from redis
  AtomicString log_level = "warning";
  AtomicString run_id;
  std::atomic<size_t> databases = 16;
  std::atomic_uint32_t worker_threads_num = 2;
  std::atomic_uint32_t slave_threads_num = 2;
  std::atomic<size_t> db_instance_num = 3;
  std::atomic_bool use_raft = true;

  std::atomic_uint32_t rocksdb_max_subcompactions = 0;
  // default 2
  std::atomic_int rocksdb_max_background_jobs = 4;
  // default 2
  std::atomic<size_t> rocksdb_max_write_buffer_number = 2;
  // default 2
  std::atomic_int rocksdb_min_write_buffer_number_to_merge = 2;
  // default 64M
  std::atomic<size_t> rocksdb_write_buffer_size = 64 << 20;
  std::atomic_int rocksdb_level0_file_num_compaction_trigger = 4;
  std::atomic_int rocksdb_num_levels = 7;
  std::atomic_bool rocksdb_enable_pipelined_write = false;
  std::atomic_int rocksdb_level0_slowdown_writes_trigger = 20;
  std::atomic_int rocksdb_level0_stop_writes_trigger = 36;
  std::atomic_uint64_t rocksdb_ttl_second = 604800;       // default 86400 * 7
  std::atomic_uint64_t rocksdb_periodic_second = 259200;  // default 86400 * 3

  rocksdb::Options GetRocksDBOptions();

  rocksdb::BlockBasedTableOptions GetRocksDBBlockBasedTableOptions();

 private:
  inline void AddString(const std::string& key, bool rewritable, std::vector<AtomicString*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, nullptr, rewritable, values_ptr_vector));
  }
  inline void AddStrinWithFunc(const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                               std::vector<AtomicString*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, checkfunc, rewritable, values_ptr_vector));
  }
  inline void AddBool(const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                      std::atomic<bool>* value_ptr) {
    config_map_.emplace(key, std::make_unique<BoolValue>(key, checkfunc, rewritable, value_ptr));
  }
  template <typename T>
  inline void AddNumber(const std::string& key, bool rewritable, std::atomic<T>* value_ptr) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, rewritable, value_ptr));
  }
  template <typename T>
  inline void AddNumberWihLimit(const std::string& key, bool rewritable, std::atomic<T>* value_ptr, T min, T max) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, rewritable, value_ptr, min, max));
  }

 private:
  ConfigParser parser_;
  ConfigMap config_map_;
  std::string config_file_name_;
};
}  // namespace pikiwidb
