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

  bool Set(std::string value, bool from_file);

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
  bool Set(std::string, const std::string&);

 public:
  bool daemonize = "no";
  PString pidfile = "./pikiwidb.pid";

  PString ip = "127.0.0.1";
  unsigned short port = 9221;

  int timeout = 60;

  PString dbpath = "./db/";

  PString loglevel = "warning";
  PString logdir = "stdout";  // the log directory, differ from redis

  int databases = 3;

  // auth
  PString password;

  std::map<PString, PString> aliases;

  // @ rdb
  // save seconds changes
  int saveseconds;
  int savechanges;
  bool rdbcompression;  // yes
  bool rdbchecksum;     // yes
  PString rdbfullname;  // ./dump.rdb

  int maxclients = 10000;  // 10000

  int slowlogtime = 1000;   // 1000 microseconds
  int slowlogmaxlen = 128;  // 128

  int hz;  // 10  [1,500]

  PString masterIp;
  unsigned short masterPort;  // replication
  PString masterauth;

  PString runid;

  PString includefile;  // the template config

  std::vector<PString> modules;  // modules

  // use redis as cache, level db as backup
  uint64_t maxmemory;    // default 2GB
  int maxmemorySamples;  // default 5
  bool noeviction;       // default true

  // THREADED I/O
  int worker_threads_num = 2;

  // THREADED SLAVE
  int slave_threads_num = 2;

  int fast_cmd_threads_num = 12;
  int slow_cmd_threads_num = 12;

  int backend = 1;  // enum BackEndType
  PString backendPath;
  int backendHz;  // the frequency of dump to backend

  int64_t max_client_response_size;

  int db_instance_num = 3;
  uint64_t rocksdb_ttl_second = 604800;
  uint64_t rocksdb_periodic_second = 259200;

 private:
  ConfigParser parser_;
  ConfigMap config_map_;
  std::string config_file_name_;
};

extern PConfig g_config;

}  // namespace pikiwidb
