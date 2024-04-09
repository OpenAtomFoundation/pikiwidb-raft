/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "config.h"
#include "pstd/pstd_string.h"

namespace pikiwidb {

constexpr const uint16_t PORT_LIMIT_MAX = 65535;
constexpr const uint16_t PORT_LIMIT_MIN = 1;
constexpr const int DBNUMBER_MAX = 16;
constexpr const int THREAD_MAX = 129;
constexpr const int ROCKSDB_INSTANCE_NUMBER_MAX = 10;

#define AGG(x) x

#define CONFIGADDSTRING(key, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<StringValue>(key, nullptr, nullptr, rewritable, val_ptr));

#define CONFIGADDSTRINGWITHFUNC(key, checkfun, prefun, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<StringValue>(key, checkfun, prefun, rewritable, val_ptr));

//#define CONFIGADDBOOL(key, rewritable, val_ptr) \
//  config_map_.emplace(key, std::make_unique<BoolValue>(key, var, nullptr, nullptr, rewritable, val_ptr));

#define CONFIGADDBOOLWITHFUNC(key, checkfun, prefun, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<BoolValue>(key, checkfun, prefun, rewritable, val_ptr));

#define CONFIGADDNUMBER(type, key, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<NumberValue<type>>(key, nullptr, nullptr, rewritable, val_ptr));

#define CONFIGADDNUMBERWITHLIMIT(type, key, rewritable, val_ptr, min, max) \
  config_map_.emplace(key, std::make_unique<NumberValue<type>>(key, nullptr, nullptr, rewritable, val_ptr, min, max));

//#define CONFIGADDNUMBERWITHFUNC(type, key, checkfun, prefun, rewritable, val_ptr) \
//  config_map_.emplace(key,                                                               \
//                      std::make_unique<NumberValue<type>>(key, checkfun, prefun, rewritable, val_ptr));
//
// #define CONFIGADDNUMBERWITHFUNCANDLIMIT(type, key, checkfun, prefun, rewritable, val_ptr, min, max) \
//  config_map_.emplace(key,                                                               \
//                      std::make_unique<NumberValue<type>>(key, checkfun, prefun, rewritable, val_ptr, min, max));

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

// check func
static bool CheckYesNo(const std::string& value) {
  return pstd::StringEqualCaseInsensitive(value, "yes") || pstd::StringEqualCaseInsensitive(value, "no");
}

static bool CheckLogLevel(const std::string& value) {
  return pstd::StringEqualCaseInsensitive(value, "debug") || pstd::StringEqualCaseInsensitive(value, "verbose") ||
         pstd::StringEqualCaseInsensitive(value, "notice") || pstd::StringEqualCaseInsensitive(value, "warning");
}

extern std::vector<std::string> SplitString(const std::string& str, char seperator);

extern std::string MergeString(const std::vector<std::string*> values, char seperator);

PConfig g_config;

bool BaseValue::Set(std::string value, bool force) {
  if (!force && !rewritable_) {
    return false;
  }
  if (custom_process_func_ptr_) {
    custom_process_func_ptr_(value);
  }
  if (!check(value)) {
    return false;
  }
  return SetValue(value);
}

bool StringValue::SetValue(const std::string& value) {
  auto values = SplitString(value, seperator_);
  if (values.size() != values_.size()) {
    return false;
  }
  for (int i = 0; i < values_.size(); i++) {
    *values_[i] = std::move(values[i]);
  }
  return true;
}

bool BoolValue::SetValue(const std::string& value) {
  if (pstd::StringEqualCaseInsensitive(value, "yes")) {
    *value_ = true;
    return true;
  } else if (pstd::StringEqualCaseInsensitive(value, "no")) {
    *value_ = false;
    return true;
  }

  return false;
}

template <typename T>
bool NumberValue<T>::SetValue(const std::string& value) {
  T v;
  auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.length(), v);
  if (ec != std::errc()) {
    return false;
  }
  if (v < value_min_) {
    v = value_min_;
  }
  if (v > value_max_) {
    v = value_max_;
  }
  *value_ = v;
  return true;
}

PConfig::PConfig() {
  {
    CONFIGADDBOOLWITHFUNC("daemonize", &CheckYesNo, &EraseQuotes, false, &daemonize);
    CONFIGADDSTRING("ip", false, std::vector<std::string*>{&ip})
    CONFIGADDNUMBERWITHLIMIT(uint16_t, "port", false, &port, PORT_LIMIT_MIN, PORT_LIMIT_MAX);
    CONFIGADDNUMBER(uint32_t, "timeout", true, &timeout_);
    CONFIGADDSTRING("db-path", false, std::vector<std::string*>{&dbpath});
    CONFIGADDSTRINGWITHFUNC("loglevel", &CheckLogLevel, nullptr, true, std::vector<std::string*>{&loglevel});
    CONFIGADDSTRING("logfile", true, std::vector<std::string*>{&logdir});
    CONFIGADDNUMBERWITHLIMIT(size_t, "databases", false, &databases, 1, DBNUMBER_MAX);
    CONFIGADDSTRING("requirepass", true, std::vector<std::string*>{&password_})
    CONFIGADDNUMBER(uint32_t, "maxclients", true, &maxclients_);
    CONFIGADDNUMBERWITHLIMIT(uint32_t, "worker-threads", false, &worker_threads_num, 1, THREAD_MAX);
    CONFIGADDNUMBERWITHLIMIT(uint32_t, "slave-threads", false, &worker_threads_num, 1, THREAD_MAX);
    CONFIGADDNUMBER(uint32_t, "slowlog-log-slower-than", true, &slowlogtime_);
    CONFIGADDNUMBER(uint32_t, "slowlog-max-len", true, &slowlogmaxlen_);
    CONFIGADDNUMBERWITHLIMIT(size_t, "db-instance-num", true, &db_instance_num, 1, ROCKSDB_INSTANCE_NUMBER_MAX);
    CONFIGADDNUMBERWITHLIMIT(int32_t, "fast-cmd-threads-num", false, &fast_cmd_threads_num_, 1, THREAD_MAX);
    CONFIGADDNUMBERWITHLIMIT(int32_t, "slow-cmd-threads-num", false, &slow_cmd_threads_num_, 1, THREAD_MAX);
    CONFIGADDNUMBER(uint64_t, "max-client-response-size", true, &max_client_response_size_);
    CONFIGADDSTRING("runid", false, std::vector<std::string*>{&runid});
    config_map_.emplace("test", std::make_unique<StringValue>("test", nullptr, &EraseQuotes, true,
                                                              std::vector<std::string*>{&hello_, &world_}));
  }

  // rocksdb config
  {
    CONFIGADDNUMBER(uint64_t, "rocksdb-ttl-second", true, &rocksdb_ttl_second_);
    CONFIGADDNUMBER(uint64_t, "rocksdb-periodic-second", true, &rocksdb_periodic_second_);
  }
}

bool PConfig::LoadFromFile(const std::string& file_name) {
  config_file_name_ = file_name;
  if (!parser_.Load(file_name.c_str())) {
    return false;
  }

  for (auto& [key, value] : parser_.GetMap()) {
    if (auto iter = config_map_.find(key); iter == config_map_.end() || value.size() > 1) {
      continue;
    }
    assert(value.size() == 1);
    if (auto& v = config_map_[key]; !v->Set(value.at(0), true)) {
      return false;
    }
  }

  // Handle separately
  std::vector<PString> master(SplitString(parser_.GetData<PString>("slaveof"), ' '));
  if (master.size() == 2) {
    masterIp_ = master[0];
    masterPort_ = static_cast<uint16_t>(std::stoi(master[1]));
  }

  std::vector<PString> alias(SplitString(parser_.GetData<PString>("rename-command"), ' '));
  if (alias.size() % 2 == 0) {
    for (auto it(alias.begin()); it != alias.end();) {
      const PString& oldCmd = *(it++);
      const PString& newCmd = *(it++);
      aliases_[oldCmd] = newCmd;
    }
  }

  return true;
}

void PConfig::Get(const std::string& key, std::vector<std::string>* values) const {
  values->clear();
  std::shared_lock<std::shared_mutex> sharedLock(mutex_);
  for (const auto& [k, v] : config_map_) {
    if (key == "*" || pstd::StringMatch(key.c_str(), k.c_str(), 1)) {
      values->emplace_back(k);
      values->emplace_back(v->Value());
    }
  }
}

bool PConfig::Set(std::string key, const std::string& value, bool force) {
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);
  auto iter = config_map_.find(key);
  if (iter == config_map_.end() || (!force && !iter->second->ReWritable())) {
    return false;
  }
  std::lock_guard<std::shared_mutex> Lock(mutex_);
  return iter->second->Set(value, force);
}

}  // namespace pikiwidb
