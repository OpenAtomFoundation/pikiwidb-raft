/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

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

extern std::vector<PString> SplitString(const PString& str, char seperator);

PConfig g_config;

bool BaseValue::Set(std::string value, bool from_file) {
  if (!from_file && !rewritable_) {
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
  } else {
    return false;
  }
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
  // ....

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
  // ....

  {
    CONFIGADDBOOL("daemonize", daemonize, CheckYesNo, EraseQuotes, false, &daemonize);
    CONFIGADDSTRING("ip", ip, nullptr, nullptr, false, &ip)
    CONFIGADDNUMBER(unsigned short, "port", port, nullptr, nullptr, false, &port, PORT_LIMIT_MIN, PORT_LIMIT_MAX);
    CONFIGADDNUMBER(int, "timeout", timeout, nullptr, nullptr, true, &timeout, -1, INT32_MAX);
    CONFIGADDSTRING("db-path", dbpath, nullptr, nullptr, false, &dbpath);
    CONFIGADDSTRING("loglevel", loglevel, CheckLogLevel, nullptr, true, &loglevel);
    CONFIGADDSTRING("logfile", logdir, nullptr, nullptr, true, &logdir);
    CONFIGADDNUMBER(int, "databases", databases, nullptr, nullptr, false, &databases, 0, DBNUMBER_MAX);
    CONFIGADDSTRING("requirepass", password, nullptr, nullptr, true, &password)
    CONFIGADDNUMBER(int, "maxclients", maxclients, nullptr, nullptr, true, &maxclients, 0, INT32_MAX);
    CONFIGADDNUMBER(int, "worker-threads", worker_threads_num, nullptr, nullptr, false, &worker_threads_num, 0,
                    THREAD_MAX);
    CONFIGADDNUMBER(int, "slave-threads", worker_threads_num, nullptr, nullptr, false, &worker_threads_num, 0,
                    THREAD_MAX);
    CONFIGADDNUMBER(int, "slowlog-log-slower-than", slowlogtime, nullptr, nullptr, true, &slowlogtime, INT32_MIN,
                    INT32_MAX);
    CONFIGADDNUMBER(int, "slowlog-max-len", slowlogmaxlen, nullptr, nullptr, true, &slowlogmaxlen, 0, INT32_MAX);
    CONFIGADDNUMBER(int, "db-instance-num", db_instance_num, nullptr, nullptr, true, &db_instance_num, 0,
                    ROCKSDB_INSTANCE_NUMBER_MAX);
    CONFIGADDNUMBER(int, "fast-cmd-threads-num", fast_cmd_threads_num, nullptr, nullptr, false, &fast_cmd_threads_num,
                    0, THREAD_MAX);
    CONFIGADDNUMBER(int, "slow-cmd-threads-num", slow_cmd_threads_num, nullptr, nullptr, false, &slow_cmd_threads_num,
                    0, THREAD_MAX);
    CONFIGADDNUMBER(int64_t, "max-client-response-size", max_client_response_size, nullptr, nullptr, true,
                    &max_client_response_size, 0, INT64_MAX);
  }

  // rocksdb config
  {
    CONFIGADDNUMBER(uint64_t, "rocksdb-ttl-second", rocksdb_ttl_second, nullptr, nullptr, true, &rocksdb_ttl_second, 0,
                    UINT64_MAX);
    CONFIGADDNUMBER(uint64_t, "rocksdb-periodic-second", rocksdb_periodic_second, nullptr, nullptr, true,
                    &rocksdb_periodic_second, 0, UINT64_MAX);
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
      std::printf("key = %s", key.c_str());
      return false;
    }
  }
  return true;
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

bool PConfig::Set(std::string key, const std::string& value) {
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);
  auto iter = config_map_.find(key);
  if (iter == config_map_.end() || !iter->second->ReWritable()) {
    return false;
  }
  return iter->second->Set(value, false);
}

}  // namespace pikiwidb
