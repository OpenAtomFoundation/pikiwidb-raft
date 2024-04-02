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
#include <string>
#include <vector>

#include "common.h"

namespace pikiwidb {

using CheckFunc = std::function<bool(const std::string&)>;
using PreProcessFunc = std::function<std::string(const std::string&)>;

enum BackEndType {
  kBackEndNone = 0,
  kBackEndRocksDB = 1,
  kBackEndMax = 2,
};

struct PConfig {
  bool daemonize;
  PString pidfile;

  PString ip;
  unsigned short port;

  int timeout;

  PString dbpath;

  PString loglevel;
  PString logdir;  // the log directory, differ from redis

  int databases;

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

  int maxclients;  // 10000

  int slowlogtime;    // 1000 microseconds
  int slowlogmaxlen;  // 128

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
  int worker_threads_num;

  // THREADED SLAVE
  int slave_threads_num;

  int backend;  // enum BackEndType
  PString backendPath;
  int backendHz;  // the frequency of dump to backend

  int64_t max_client_response_size;

  int db_instance_num;
  uint64_t rocksdb_ttl_second;
  uint64_t rocksdb_periodic_second;
  PConfig();

  bool CheckArgs() const;
  bool CheckPassword(const PString& pwd) const;
};

extern PConfig g_config;

extern bool LoadPikiwiDBConfig(const char* cfgFile, PConfig& cfg);

class BaseValue {
 public:
  BaseValue(const std::string& key, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
            bool rewritable = false)
      : key_(key), custom_check_func_ptr_(check_func_ptr), rewritable_(rewritable) {}

  virtual ~BaseValue(){};

  std::string Key() const { return key_; }

  virtual const std::string& Value() = 0;

  bool SetValue(const std::string& value);

  bool SupportDynamicSet() { return rewritable_; }

 protected:
  virtual void SetValuePrivate(const std::string&) = 0;
  bool check(const std::string& value) {
    if (custom_check_func_ptr_ && !custom_check_func_ptr_(value)) {
      return false;
    }
    return true;
  }

 protected:
  std::string key_ = "";
  CheckFunc custom_check_func_ptr_ = nullptr;
  PreProcessFunc custom_process_func_ptr_ = nullptr;
  bool rewritable_ = false;
};

class StringValue : public BaseValue {
 public:
  StringValue(const std::string& key, std::string value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
              bool support_dynamic_set = false)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, support_dynamic_set) {
    value_ = std::move(value);
  };

  virtual const std::string& Value() override { return value_; };

 private:
  virtual void SetValuePrivate(const std::string&) override;

  std::string value_ = "";
};

template <typename T>
class NumberValue : public BaseValue {
 public:
  NumberValue(const std::string& key, T value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
              T max = std::numeric_limits<T>::max(), T min = std::numeric_limits<T>::min(),
              bool support_dynamic_set = false)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, support_dynamic_set) {
    value_ = value;
    value_max_ = max;
    value_min_ = min;
    string_value_ = std::move(std::to_string(value));
  };

  virtual const std::string& Value() override { return string_value_; }

 private:
  virtual void SetValuePrivate(const std::string&) override;

  T value_;
  T value_max_;
  T value_min_;
  std::string string_value_ = "";
};

class BoolValue : public BaseValue {
 public:
  BoolValue(const std::string& key, bool value, CheckFunc check_func_ptr, PreProcessFunc preprocess_func_ptr,
            bool support_dynamic_set = false)
      : BaseValue(key, check_func_ptr, preprocess_func_ptr, support_dynamic_set) {
    value_ = std::move(value);
    string_value_ = value_ ? "yes" : "no";
  };

  virtual const std::string& Value() override { return string_value_; };

 private:
  virtual void SetValuePrivate(const std::string&) override;

  bool value_;
  std::string string_value_ = "";
};

// TODO other Value

struct PikiwiDBConfig {
  bool daemonize = false;
  PString pidfile = "/var/run/pikiwidb.pid";
  PString ip = "127.0.0.1";
  unsigned short port = 9221;
  int timeout = 0;
  PString dbpath = "./db";
  PString loglevel = "warning";
  PString logdir = "stdout";
  int databases = 3;

  // auth
  PString password = "";

  //    std::map<PString, PString> aliases;

  // @ rdb
  // save seconds changes
  int saveseconds = 999999999;
  int savechanges = 999999999;
  bool rdbcompression = true;          // yes
  bool rdbchecksum = true;             // yes
  PString rdbfullname = "./dump.rdb";  // ./dump.rdb

  int maxclients = 1000;  // 10000

  int slowlogtime = 1000;   // 1000 microseconds
  int slowlogmaxlen = 128;  // 128

  int hz = 10;  // 10  [1,500]

  PString masterIp = "";
  unsigned short masterPort = 0;  // replication
  PString masterauth = "";

  PString runid = "";

  PString includefile = "";  // the template config

  // use redis as cache, level db as backup
  uint64_t maxmemory = 2 * 1024 * 1024 * 1024UL;  // default 2GB
  int maxmemorySamples = 5;                       // default 5
  bool noeviction = true;                         // default true

  // THREADED I/O
  int worker_threads_num = 2;

  // THREADED SLAVE
  int slave_threads_num = 2;

  int backend = kBackEndRocksDB;  // enum BackEndType
  PString backendPath = "dump";
  int backendHz = 10;  // the frequency of dump to backend

  int64_t max_client_response_size = 1073741824;

  int db_instance_num = 3;
};

struct RocksDBConfig {
  uint64_t rocksdb_ttl_second = 0;
  uint64_t rocksdb_periodic_second = 0;
};

// TODO other config

using FieldPrt = std::unique_ptr<BaseValue>;
using ConfigMap = std::unordered_map<std::string, FieldPrt>;

class Config {
 public:
  Config();
  ~Config();

 public:
  PikiwiDBConfig pikiwidb_config;
  RocksDBConfig rocksdb_config;
  bool LoadFromFile(const std::string& file_name);

  // 提供访问 config_map_ 的接口.

 private:
  std::string config_file_name_ = "";
  ConfigMap config_map_;
};

}  // namespace pikiwidb
