/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "pstd/pstd_string.h"

namespace pikiwidb {

constexpr const unsigned short PORT_LIMIT_MAX = 65535;
constexpr const unsigned short PORT_LIMIT_MIN = 1;
constexpr const int DBNUMBER_MAX = 16;
constexpr const int THREAD_MAX = 129;
constexpr const int ROCKSDB_INSTANCE_NUMBER_MAX = 10;

#define CONFIGADDSTRING(key, var, checkfun, prefun, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<StringValue>(key, var, checkfun, prefun, rewritable, val_ptr));

#define CONFIGADDBOOL(key, var, checkfun, prefun, rewritable, val_ptr) \
  config_map_.emplace(key, std::make_unique<BoolValue>(key, var, checkfun, prefun, rewritable, val_ptr));

#define CONFIGADDNUMBER(type, key, var, checkfun, prefun, rewritable, val_ptr, min, max) \
  config_map_.emplace(key,                                                               \
                      std::make_unique<NumberValue<type>>(key, var, checkfun, prefun, rewritable, val_ptr, min, max));

static void EraseQuotes(PString& str) {
  // convert "hello" to  hello
  if (str.size() < 2) {
    return;
  }
  if (str[0] == '"' && str[str.size() - 1] == '"') {
    str.erase(str.begin());
    str.pop_back();
  }
}

extern std::vector<PString> SplitString(const PString& str, char seperator);

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
  *value_ = std::move(value);
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
  std::istringstream iss(value);
  iss >> v;
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
  // preprocess func
  auto EraseQuotes = [](std::string& value) {
    if (value.size() < 2) {
      return;
    }
    if (value[0] == '"' && value[value.size() - 1] == '"') {
      value.erase(value.begin());
      value.pop_back();
    }
  };

  // check func
  auto CheckYesNo = [](const std::string& value) -> bool {
    if (pstd::StringEqualCaseInsensitive(value, "yes") || pstd::StringEqualCaseInsensitive(value, "no")) {
      return true;
    }
    return false;
  };
  auto CheckLogLevel = [](const std::string& value) -> bool {
    if (pstd::StringEqualCaseInsensitive(value, "debug") || pstd::StringEqualCaseInsensitive(value, "verbose") ||
        pstd::StringEqualCaseInsensitive(value, "notice") || pstd::StringEqualCaseInsensitive(value, "warning")) {
      return true;
    }
    return false;
  };

  {
    CONFIGADDBOOL("daemonize", daemonize_, CheckYesNo, EraseQuotes, false, &daemonize_);
    CONFIGADDSTRING("ip", ip_, nullptr, nullptr, false, &ip_)
    CONFIGADDNUMBER(unsigned short, "port", port_, nullptr, nullptr, false, &port_, PORT_LIMIT_MIN, PORT_LIMIT_MAX);
    CONFIGADDNUMBER(int, "timeout", timeout_, nullptr, nullptr, true, &timeout_, -1, INT32_MAX);
    CONFIGADDSTRING("db-path", dbpath_, nullptr, nullptr, false, &dbpath_);
    CONFIGADDSTRING("loglevel", loglevel_, CheckLogLevel, nullptr, true, &loglevel_);
    CONFIGADDSTRING("logfile", logdir_, nullptr, nullptr, true, &logdir_);
    CONFIGADDNUMBER(int, "databases", databases_, nullptr, nullptr, false, &databases_, 0, DBNUMBER_MAX);
    CONFIGADDSTRING("requirepass", password_, nullptr, nullptr, true, &password_)
    CONFIGADDNUMBER(int, "maxclients", maxclients_, nullptr, nullptr, true, &maxclients_, 0, INT32_MAX);
    CONFIGADDNUMBER(int, "worker-threads", worker_threads_num_, nullptr, nullptr, false, &worker_threads_num_, 0,
                    THREAD_MAX);
    CONFIGADDNUMBER(int, "slave-threads", worker_threads_num_, nullptr, nullptr, false, &worker_threads_num_, 0,
                    THREAD_MAX);
    CONFIGADDNUMBER(int, "slowlog-log-slower-than", slowlogtime_, nullptr, nullptr, true, &slowlogtime_, INT32_MIN,
                    INT32_MAX);
    CONFIGADDNUMBER(int, "slowlog-max-len", slowlogmaxlen_, nullptr, nullptr, true, &slowlogmaxlen_, 0, INT32_MAX);
    CONFIGADDNUMBER(int, "db-instance-num", db_instance_num_, nullptr, nullptr, true, &db_instance_num_, 0,
                    ROCKSDB_INSTANCE_NUMBER_MAX);
    CONFIGADDNUMBER(int, "fast-cmd-threads-num", fast_cmd_threads_num_, nullptr, nullptr, false, &fast_cmd_threads_num_,
                    0, THREAD_MAX);
    CONFIGADDNUMBER(int, "slow-cmd-threads-num", slow_cmd_threads_num_, nullptr, nullptr, false, &slow_cmd_threads_num_,
                    0, THREAD_MAX);
    CONFIGADDNUMBER(int64_t, "max-client-response-size", max_client_response_size_, nullptr, nullptr, true,
                    &max_client_response_size_, 0, INT64_MAX);
    CONFIGADDSTRING("runid", runid_, nullptr, nullptr, false, &runid_)
  }

  // rocksdb config
  {
    CONFIGADDNUMBER(uint64_t, "rocksdb-ttl-second", rocksdb_ttl_second_, nullptr, nullptr, true, &rocksdb_ttl_second_,
                    0, UINT64_MAX);
    CONFIGADDNUMBER(uint64_t, "rocksdb-periodic-second", rocksdb_periodic_second_, nullptr, nullptr, true,
                    &rocksdb_periodic_second_, 0, UINT64_MAX);
    // ....
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
    masterPort_ = static_cast<unsigned short>(std::stoi(master[1]));
  }

  std::vector<PString> saveInfo(SplitString(parser_.GetData<PString>("save"), ' '));
  if (!saveInfo.empty() && saveInfo.size() != 2) {
    EraseQuotes(saveInfo[0]);
    if (!(saveInfo.size() == 1 && saveInfo[0].empty())) {
      std::cerr << "bad format save rdb interval, bad string " << parser_.GetData<PString>("save") << std::endl;
      return false;
    }
  } else if (!saveInfo.empty()) {
    saveseconds_ = std::stoi(saveInfo[0]);
    savechanges_ = std::stoi(saveInfo[1]);
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
