/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <strings.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "unbounded_buffer.h"

#define CRLF "\r\n"

using PString = std::string;

namespace pikiwidb {

const int kStringMaxBytes = 1 * 1024 * 1024 * 1024;

#define PIKIWIDB_SCAN_STEP_LENGTH 1000

enum PError {
  kPErrorNop = -1,
  kPErrorOK = 0,
  kPErrorType = 1,
  kPErrorExist = 2,
  kPErrorNotExist = 3,
  kPErrorParam = 4,
  kPErrorUnknowCmd = 5,
  kPErrorNan = 6,
  kPErrorSyntax = 7,
  kPErrorDirtyExec = 8,
  kPErrorWatch = 9,
  kPErrorNoMulti = 10,
  kPErrorInvalidDB = 11,
  kPErrorReadonlySlave = 12,
  kPErrorNeedAuth = 13,
  kPErrorErrAuth = 14,
  kPErrorNomodule = 15,
  kPErrorModuleinit = 16,
  kPErrorModuleuninit = 17,
  kPErrorModulerepeat = 18,
  kPErrorOverflow = 19,
  kPErrorMax,
};

extern struct PErrorInfo {
  int len;
  const char* errorStr;
} g_errorInfo[];

int StrToLongDouble(const char* s, size_t slen, long double* ldval);

std::size_t FormatInt(long value, UnboundedBuffer* reply);
std::size_t FormatBulk(const char* str, std::size_t len, UnboundedBuffer* reply);
std::size_t FormatBulk(const PString& str, UnboundedBuffer* reply);
std::size_t PreFormatMultiBulk(std::size_t nBulk, UnboundedBuffer* reply);

std::size_t FormatOK(UnboundedBuffer* reply);

void ReplyError(PError err, UnboundedBuffer* reply);

enum class PParseResult : int8_t {
  kOK,
  kWait,
  kError,
};

PParseResult GetIntUntilCRLF(const char*& ptr, std::size_t nBytes, int& val);

class AtomicString {
 public:
  AtomicString() = default;
  ~AtomicString() = default;
  AtomicString(std::string str) {
    std::lock_guard lock(mutex_);
    str_ = std::move(str);
  }
  AtomicString(std::string&& str) {
    std::lock_guard lock(mutex_);
    str_ = std::move(str);
  }
  AtomicString(const std::string& str) {
    std::lock_guard lock(mutex_);
    str_ = str;
  }
  AtomicString(const char* c) {
    std::lock_guard lock(mutex_);
    str_ = std::string(c);
  };
  AtomicString& operator=(const std::string& str) {
    std::lock_guard lock(mutex_);
    str_ = str;
    return *this;
  }
  AtomicString& operator=(std::string&& str) {
    std::lock_guard lock(mutex_);
    str_ = std::move(str);
    return *this;
  }
  operator std::string() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return str_;
  }

  operator std::string() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return str_;
  }

  bool empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return str_.empty();
  }

  std::string ToString() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return str_;
  }

 private:
  mutable std::shared_mutex mutex_;
  std::string str_;
};

std::vector<PString> SplitString(const PString& str, char seperator);

std::string MergeString(const std::vector<std::string*>& values, char delimiter);

std::string MergeString(const std::vector<AtomicString*>& values, char delimiter);

// The defer class for C++11
class ExecuteOnScopeExit {
 public:
  ExecuteOnScopeExit() = default;

  ExecuteOnScopeExit(ExecuteOnScopeExit&& e) noexcept { func_ = std::move(e.func_); }

  ExecuteOnScopeExit(const ExecuteOnScopeExit& e) = delete;
  void operator=(const ExecuteOnScopeExit& f) = delete;

  template <typename F, typename... Args>
  ExecuteOnScopeExit(F&& f, Args&&... args) {
    auto temp = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    func_ = [temp]() { (void)temp(); };
  }

  ~ExecuteOnScopeExit() noexcept {
    if (func_) {
      func_();
    }
  }

 private:
  std::function<void()> func_;
};

#define CONCAT(a, b) a##b
#define _MAKE_DEFER_HELPER_(line) pikiwidb::ExecuteOnScopeExit CONCAT(defer, line) = [&]()

#define DEFER _MAKE_DEFER_HELPER_(__LINE__)

}  // namespace pikiwidb
