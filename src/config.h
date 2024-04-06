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

enum BackEndType {
  kBackEndNone = 0,
  kBackEndRocksDB = 1,
  kBackEndMax = 2,
};

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

  bool Set(std::string value, bool force = false);

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
  StringValue(const std::string& key, std::string value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
              bool rewritable, std::string* value_ptr)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable), value_(value_ptr) {
    *value_ = std::move(value);
  };
  virtual ~StringValue() = default;

  virtual std::string Value() const override { return *value_; };

 private:
  virtual bool SetValue(const std::string&) override;

  std::string* value_ = nullptr;
};

template <typename T>
class NumberValue : public BaseValue {
 public:
  NumberValue(const std::string& key, T value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
              bool rewritable, T* value_ptr, T min = std::numeric_limits<T>::min(),
              T max = std::numeric_limits<T>::max())
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable),
        value_(value_ptr),
        value_max_(max),
        value_min_(min){};

  virtual std::string Value() const override { return std::to_string(*value_); }

 private:
  virtual bool SetValue(const std::string&) override;

  T* value_;
  T value_max_;
  T value_min_;
};

class BoolValue : public BaseValue {
 public:
  BoolValue(const std::string& key, bool value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
            bool rewritable, bool* value_ptr)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, rewritable), value_(value_ptr) {
    *value_ = std::move(value);
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
  bool GetDaemonize() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return daemonize_;
  }

  std::string GetPidfile() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return pidfile_;
  }

  std::string GetIp() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return ip_;
  }

  unsigned short GetPort() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return port_;
  }

  int GetTimeout() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return timeout_;
  }

  std::string GetDBPath() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return dbpath_;
  }

  std::string GetLogLevel() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return loglevel_;
  }

  std::string GetLogDir() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return logdir_;
  }

  int GetDataBases() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return databases_;
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

  int GetWorkerThreadsNumber() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return worker_threads_num_;
  }

  int GetSlaveThreadsNumber() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return slave_threads_num_;
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

  int GetDBInstanceNumber() const {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return db_instance_num_;
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

  int GetBackEndType() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return backend_;
  }

  std::string GetMasterIP() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return masterIp_;
  }

  unsigned short GetMasterPort() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return masterPort_;
  }

  int GetHZ() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return hz_;
  }

  std::string GetRDBFullName() {
    std::shared_lock<std::shared_mutex> SharedLock(mutex_);
    return rdbfullname_;
  }

 private:
  mutable std::shared_mutex mutex_;
  bool daemonize_ = false;
  PString pidfile_ = "./pikiwidb.pid";

  PString ip_ = "127.0.0.1";
  unsigned short port_ = 9221;

  int timeout_ = 60;

  PString dbpath_ = "./db/";

  PString loglevel_ = "warning";
  PString logdir_ = "stdout";  // the log directory, differ from redis

  int databases_ = 3;

  // auth
  PString password_;

  std::map<PString, PString> aliases_;

  // @ rdb
  // save seconds changes
  int saveseconds_;
  int savechanges_;
  bool rdbcompression_;  // yes
  bool rdbchecksum_;     // yes
  PString rdbfullname_;  // ./dump.rdb

  int maxclients_ = 10000;  // 10000

  int slowlogtime_ = 1000;   // 1000 microseconds
  int slowlogmaxlen_ = 128;  // 128

  int hz_;  // 10  [1,500]

  PString masterIp_;
  unsigned short masterPort_;  // replication
  PString masterauth_;

  PString runid_;

  PString includefile_;  // the template config

  std::vector<PString> modules_;  // modules

  // use redis as cache, level db as backup
  uint64_t maxmemory_;    // default 2GB
  int maxmemorySamples_;  // default 5
  bool noeviction_;       // default true

  // THREADED I/O
  int worker_threads_num_ = 2;

  // THREADED SLAVE
  int slave_threads_num_ = 2;

  int fast_cmd_threads_num_ = 12;
  int slow_cmd_threads_num_ = 12;

  int backend_ = 1;  // enum BackEndType
  PString backendPath_;
  int backendHz_;  // the frequency of dump to backend

  int64_t max_client_response_size_ = 1073741824;

  int db_instance_num_ = 3;
  uint64_t rocksdb_ttl_second_ = 604800;
  uint64_t rocksdb_periodic_second_ = 259200;

 private:
  ConfigParser parser_;
  ConfigMap config_map_;
  std::string config_file_name_;
};

extern PConfig g_config;

}  // namespace pikiwidb
