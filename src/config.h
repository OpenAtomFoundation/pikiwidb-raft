// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Declared a set of functions responsible for managing the
  runtime configuration information of PikiwiDB.
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

/*
 * PConfig holds information about PikiwiDB
 * server-side runtime information.
 */
class PConfig {
 public:
  /* Some important, globally relevant public interfaces. */

  /*------------------------
   * PConfig()
   * Initialize PikiwiDB's config & RocksDB's config.
   */
  PConfig();

  /*------------------------
   * ~PConfig()
   * Destroy a PikiwiDB instance.
   */
  ~PConfig() = default;

  /*------------------------
   * LoadFromFile(const std::string& file_name)
   * Load a PikiwiDB config file and store in PConfig
   * Check the return to see success or not
   */
  bool LoadFromFile(const std::string& file_name);

  /*------------------------
   * ConfigFileName()
   * Return the name of the configuration file name specified at load time.
   */
  const std::string& ConfigFileName() const { return config_file_name_; }

  /*------------------------
   * Get (const std::string& key, std::vector<std::string>* values)
   * Retrieve the data corresponding to the specified configuration parameter.
   * key is the configuration parameter want to retrieve
   * values store the result
   */
  void Get(const std::string& key, std::vector<std::string>* values) const;

  /*------------------------
   * Set (std::string key, const std::string& value, bool init_stage)
   * Set the data for the specified parameter。
   * key is the configuration parameter want to set, case insensitive
   * value is the data want to storexs
   * init_stage represents whether the system is in the initialization phase,
   * some parameters are not allowed to be modified outside of the initialization phase
   */
  Status Set(std::string key, const std::string& value, bool init_stage = false);

 public:
  /*
   * Some crucial, globally significant, externally accessible public data.
   * Refer to the pikiwidb.conf
   */

  /*
   * If the connection times out, then terminate the connection.
   * 0 means no timeout limit, otherwise it is the specified number of seconds.
   */
  std::atomic_uint32_t timeout = 0;
  /*
   * Client connect to PikiwiDB server may need password
   */
  AtomicString password;

  /*
   * Slave node connect to Master node may need password(master_auth),
   * and their need to keep the master_ip & master_port
   */
  AtomicString master_auth;
  AtomicString master_ip;
  std::atomic_uint32_t master_port;

  // aliases store the rename command
  std::map<std::string, std::string> aliases;

  // The max connection limition
  std::atomic_uint32_t max_clients = 10000;

  /*
   * Slow log help us to know the command that execute time more
   * than slow_log_time (in microseconds, 1000000 microseconds
   * is equivalent to 1 second)
   *
   * slow_log_max_len in current version just consume memory,
   * has no practical effect
   */
  std::atomic_uint32_t slow_log_time = 1000;
  std::atomic_uint32_t slow_log_max_len = 128;

  /*
   * include_file & modules in current version not support
   * Reference: https://redis.io/docs/latest/operate/oss_and_stack/management/config-file/
   */
  AtomicString include_file;
  std::vector<PString> modules;

  /*
   * PikiwiDB use the thread pool to manage the task,
   * categorize them into two types: fast tasks and slow tasks,
   * and fast_cmd_threads_num & slow_cmd_threads_num used to set
   * the number of threads to handle these task.
   *
   * In current version, we only use the fast task thread pool.
   *
   */
  std::atomic_int32_t fast_cmd_threads_num = 4;
  std::atomic_int32_t slow_cmd_threads_num = 4;

  // Limit the maximum number of bytes returned to the client.
  std::atomic_uint64_t max_client_response_size = 1073741824;

  /*
   * Decide when to trigger a small-scale merge operation.
   * In default, small_compaction_threshold = 86400 * 7,
   * small_compaction_duration_threshold = 86400 * 3.
   */
  std::atomic_uint64_t small_compaction_threshold = 604800;
  std::atomic_uint64_t small_compaction_duration_threshold = 259200;

  // Decide whether PikiwiDB runs as a daemon process.
  std::atomic_bool daemonize = false;

  // Which file to store the process id when running?
  AtomicString pid_file = "./pikiwidb.pid";

  /*
   * For PikiwiDB, ip is the address and the port that
   * the server will listen on.
   * In default, the full address will be "127.0.0.1:9221"
   */
  AtomicString ip = "127.0.0.1";
  std::atomic_uint16_t port = 9221;

  /*
   * The raft protocol need regular communication between nodes.
   * We will set the port that will ultimately be used
   * for communication to be the port + raft_port_offset
   * In default, raft_port_offset = 10
   */
  std::atomic_uint16_t raft_port_offset = 10;

  // The path to store the data
  AtomicString db_path = "./db/";

  // The log directory, default print to stdout
  AtomicString log_dir = "stdout";

  /*
   * PikiwiDB uses the SPDLOG Library to implement the log module,
   * so the log_level is the same as the SPDLOG level.
   * Just look at SPDLOG wiki to know more.
   */
  AtomicString log_level = "warning";

  /*
   * run_id is a SHA1-sized random number that identifies a
   * given execution of PikiwiDB.
   */
  AtomicString run_id;

  // The number of databases.
  std::atomic<size_t> databases = 16;

  /*
   * For Network I/O threads, in future version, we may delete
   * slave_threads_num.
   */
  std::atomic_uint32_t worker_threads_num = 2;
  std::atomic_uint32_t slave_threads_num = 2;

  // How many RocksDB Instances will be opened?
  std::atomic<size_t> db_instance_num = 3;

  // Use raft protocol?
  std::atomic_bool use_raft = true;

  /*
   * PikiwiDB use the RocksDB to store the data,
   * and these options below will set to rocksdb::Options,
   * Just check the RocksDB document & PConfig::GetRocksDBOptions
   * to know more.
   */

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

  // 86400 * 7 = 604800
  std::atomic_uint64_t rocksdb_ttl_second = 604800;

  // 86400 * 3 = 259200
  std::atomic_uint64_t rocksdb_periodic_second = 259200;

  // cache
  std::vector<std::string> cache_type_all;
  std::atomic_bool tmp_cache_disable_flag = false;
  std::atomic_uint64_t cache_maxmemory = 10737418240;
  std::atomic_int cache_num = 5;
  std::atomic_int cache_mode = 1;
  std::atomic_int cache_string = 0;
  std::atomic_int cache_set = 0;
  std::atomic_int cache_zset = 0;
  std::atomic_int cache_hash = 0;
  std::atomic_int cache_list = 0;
  std::atomic_int cache_bit = 0;
  std::atomic_int zset_cache_start_direction = 0;
  std::atomic_int zset_cache_field_num_per_key = 512;
  std::atomic_int cache_maxmemory_policy = 1;
  std::atomic_int cache_maxmemory_samples = 5;
  std::atomic_int cache_lfu_decay_time = 1;

  rocksdb::Options GetRocksDBOptions();

  rocksdb::BlockBasedTableOptions GetRocksDBBlockBasedTableOptions();

  void SetCacheType(const std::string& value);

 private:
  // Some functions and variables set up for internal work.

  /*------------------------
   * AddString (const std::string& key, bool rewritable, * std::vector<AtomicString*> values_ptr_vector)
   * Introduce a new string key-value pair into the
   * configuration data layer.
   * A key may correspond to multiple values, so we use std::vector
   * to store the data.
   * rewritable represents whether to overwrite existing settings
   * when a key-value pair is duplicated.
   */
  inline void AddString(const std::string& key, bool rewritable, std::vector<AtomicString*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, nullptr, rewritable, values_ptr_vector));
  }

  /*------------------------
   * AddStringWithFunc (const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                               std::vector<AtomicString*> values_ptr_vector)
   * Introduce a new string key-value pair into the
   * configuration data layer, with a check function.
   * key, value, rewritable is the same as AddString.
   * The checkfunc is coded by the user, validate the string as needed,
   * and the return value should refer to rocksdb::Status.
   */
  inline void AddStringWithFunc(const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                                std::vector<AtomicString*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, checkfunc, rewritable, values_ptr_vector));
  }

  /*------------------------
   * AddBool (const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                      std::atomic<bool>* value_ptr)
   * Introduce a new string key-value pair into the
   * configuration data layer, with a check function.
   * key is a string and value_ptr is a point to a bool value.
   * checkfunc is the same as AddStrinWithFunc.
   * rewritable represents whether to overwrite existing settings
   * when a key-value pair is duplicated.
   */
  inline void AddBool(const std::string& key, const CheckFunc& checkfunc, bool rewritable,
                      std::atomic<bool>* value_ptr) {
    config_map_.emplace(key, std::make_unique<BoolValue>(key, checkfunc, rewritable, value_ptr));
  }

  /*------------------------
   * AddNumber (const std::string& key, bool rewritable, std::atomic<T>* value_ptr)
   * Introduce a new string key-value pair into the
   * configuration data layer, with a check function.
   * key is a string and value_ptr is a set of numbers.
   * rewritable represents whether to overwrite existing settings
   * when a key-value pair is duplicated.
   */
  template <typename T>
  inline void AddNumber(const std::string& key, bool rewritable, std::atomic<T>* value_ptr) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, rewritable, value_ptr));
  }

  /*------------------------
   * AddNumberWithLimit (const std::string& key, bool rewritable, std::atomic<T>* value_ptr, T min, T max)
   * Introduce a new string key-value pair into the
   * configuration data layer, with a check function.
   * key is a string and value_ptr is a set of numbers.
   * rewritable represents whether to overwrite existing settings
   * when a key-value pair is duplicated.
   * Please note that this function does not have a checkfunc function,
   * as we have replaced it with the upper and lower limits
   * of the numbers passed in.
   */
  template <typename T>
  inline void AddNumberWithLimit(const std::string& key, bool rewritable, std::atomic<T>* value_ptr, T min, T max) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, rewritable, value_ptr, min, max));
  }

 private:
  // The parser to parse the config data
  ConfigParser parser_;

  // Store the key-value data for config
  ConfigMap config_map_;

  // The file name of the config
  std::string config_file_name_;
};
}  // namespace pikiwidb
