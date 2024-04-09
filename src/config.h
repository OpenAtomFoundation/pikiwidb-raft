/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "net/config_parser.h"

namespace pikiwidb {

using CheckFunc = std::function<bool(const std::string&)>;
using PreProcessFunc = std::function<void(std::string&)>;

class BaseValue {
 public:
  BaseValue(const std::string& key, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
            bool rewritable = false)
      : key_(key),
        custom_check_func_ptr_(check_func_ptr),
        custom_process_func_ptr_(preprocess_func_ptr),
        rewritable_(rewritable) {}

  virtual ~BaseValue() = default;

  const std::string& Key() const { return key_; }

  virtual std::string Value() const = 0;

  bool Set(std::string value, bool force);

  bool ReWritable() { return rewritable_; }

 protected:
  virtual bool SetValue(const std::string&) = 0;
  bool check(const std::string& value) {
    if (custom_check_func_ptr_ && !custom_check_func_ptr_(value)) {
      return false;
    }
    return true;
  }

 protected:
  std::string key_;
  CheckFunc custom_check_func_ptr_ = nullptr;
  PreProcessFunc custom_process_func_ptr_ = nullptr;
  bool rewritable_ = false;
};

class StringValue : public BaseValue {
 public:
  StringValue(const std::string& key, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr, bool rewritable,
              const std::vector<std::string*>& value_ptr_vec, char seperator = ' ')
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable), values_(value_ptr_vec), seperator_(seperator) {
    assert(values_.size() >= 1);
  }
  virtual ~StringValue() = default;

  virtual std::string Value() const override { return MergeString(values_, seperator_); };

 private:
  virtual bool SetValue(const std::string&) override;

  std::vector<std::string*> values_;
  char seperator_;
};

template <typename T>
class NumberValue : public BaseValue {
 public:
  NumberValue(const std::string& key, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr, bool rewritable,
              T* value_ptr, T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max())
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable),
        value_(value_ptr),
        value_min_(min),
        value_max_(max) {
    assert(value_ != nullptr);
    assert(value_min_ <= value_max_);
  };

  virtual std::string Value() const override { return std::to_string(*value_); }

 private:
  virtual bool SetValue(const std::string&) override;

  T* value_;
  T value_min_;
  T value_max_;
};

class BoolValue : public BaseValue {
 public:
  BoolValue(const std::string& key, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr, bool rewritable,
            bool* value_ptr)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable), value_(value_ptr) {
    assert(value_ != nullptr);
  };

  virtual std::string Value() const override { return *value_ ? "yes" : "no"; };

 private:
  virtual bool SetValue(const std::string&) override;
  bool* value_;
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
  bool Set(std::string, const std::string&, bool force = false);

 public:
  int GetTimeout() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return timeout_;
  }

  std::string GetPassword() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return password_;
  }

  int GetSlowlogTime() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return slowlogtime_;
  }

  int GetSlowlogMaxLen() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return slowlogmaxlen_;
  }

  int GetFastCmdThreadsNumber() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return fast_cmd_threads_num_;
  }

  int GetSlowCmdThreadsNumber() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return slow_cmd_threads_num_;
  }

  int64_t GetMaxClientResponseSize() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return max_client_response_size_;
  }

  uint64_t GetRocksDBTTLSeconds() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return rocksdb_ttl_second_;
  }

  uint64_t GetRocksDBPeriodicSeconds() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return rocksdb_periodic_second_;
  }

  std::string GetMasterAuth() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return masterauth_;
  }

  std::string GetMasterIP() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return masterIp_;
  }

  uint16_t GetMasterPort() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return masterPort_;
  }

 private:
  inline void AddString(const std::string& key, bool rewritable, std::vector<std::string*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, nullptr, nullptr, rewritable, values_ptr_vector));
  }
  inline void AddStrinWithFunc(const std::string& key, CheckFunc checkfunc, PreProcessFunc prefunc, bool rewritable, std::vector<std::string*> values_ptr_vector) {
    config_map_.emplace(key, std::make_unique<StringValue>(key, checkfunc, prefunc, rewritable, values_ptr_vector));
  }
  inline void AddBool(const std::string& key, CheckFunc checkfunc, PreProcessFunc prefunc, bool rewritable, bool* value_ptr) {
    config_map_.emplace(key, std::make_unique<BoolValue>(key, checkfunc, prefunc, rewritable, value_ptr));
  }
  template <typename T>
  inline void AddNumber(const std::string& key, bool rewritable, T* value_ptr) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, nullptr, rewritable, value_ptr));
  }
  template <typename T>
  inline void AddNumberWihLimit(const std::string& key, bool rewritable, T* value_ptr, T min, T max) {
    config_map_.emplace(key, std::make_unique<NumberValue<T>>(key, nullptr, nullptr, rewritable, value_ptr, min, max));
  }


 public:
  // read only
  bool daemonize = false;
  std::string pidfile = "./pikiwidb.pid";
  std::string ip = "127.0.0.1";
  uint16_t port = 9221;
  std::string dbpath = "./db/";
  std::string logdir = "stdout";  // the log directory, differ from redis
  std::string loglevel = "warning";
  std::string runid;
  size_t databases = 3;
  uint32_t worker_threads_num = 2;
  uint32_t slave_threads_num = 2;
  size_t db_instance_num = 3;

 private:
  // rewritable
  mutable std::shared_mutex mutex_;
  uint32_t timeout_ = 0;
  // auth
  std::string password_ = "";
  std::map<std::string, std::string> aliases_;
  uint32_t maxclients_ = 10000;   // 10000
  uint32_t slowlogtime_ = 1000;   // 1000 microseconds
  uint32_t slowlogmaxlen_ = 128;  // 128
  std::string masterIp_;
  std::string test;
  uint16_t masterPort_;  // replication
  std::string masterauth_;
  std::string includefile_;       // the template config
  std::vector<PString> modules_;  // modules
  int32_t fast_cmd_threads_num_ = 12;
  int32_t slow_cmd_threads_num_ = 12;
  uint64_t max_client_response_size_ = 1073741824;
  uint64_t rocksdb_ttl_second_ = 604800;
  uint64_t rocksdb_periodic_second_ = 259200;

  ConfigParser parser_;
  ConfigMap config_map_;
  std::string config_file_name_;
};

extern PConfig g_config;

}  // namespace pikiwidb
