/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "common.h"
#include <math.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include "unbounded_buffer.h"

namespace pikiwidb {

struct PErrorInfo g_errorInfo[] = {
    {sizeof "+OK\r\n" - 1, "+OK\r\n"},
    {sizeof "-ERR Operation against a key holding the wrong kind of value\r\n" - 1,
     "-ERR Operation against a key holding the wrong kind of value\r\n"},
    {sizeof "-ERR already exist" - 1, "-ERR already exist"},
    {sizeof "-ERR no such key\r\n" - 1, "-ERR no such key\r\n"},
    {sizeof "-ERR wrong number of arguments\r\n" - 1, "-ERR wrong number of arguments\r\n"},
    {sizeof "-ERR Unknown command\r\n" - 1, "-ERR Unknown command\r\n"},
    {sizeof "-ERR value is not an integer or out of range\r\n" - 1, "-ERR value is not an integer or out of range\r\n"},
    {sizeof "-ERR syntax error\r\n" - 1, "-ERR syntax error\r\n"},

    {sizeof "-EXECABORT Transaction discarded because of previous errors.\r\n" - 1,
     "-EXECABORT Transaction discarded because of previous errors.\r\n"},
    {sizeof "-WATCH inside MULTI is not allowed\r\n" - 1, "-WATCH inside MULTI is not allowed\r\n"},
    {sizeof "-EXEC without MULTI\r\n" - 1, "-EXEC without MULTI\r\n"},
    {sizeof "-ERR invalid DB index\r\n" - 1, "-ERR invalid DB index\r\n"},
    {sizeof "-READONLY You can't write against a read only slave.\r\n" - 1,
     "-READONLY You can't write against a read only slave.\r\n"},
    {sizeof "-ERR operation not permitted\r\n" - 1, "-ERR operation not permitted\r\n"},
    {sizeof "-ERR invalid password\r\n" - 1, "-ERR invalid password\r\n"},
    {sizeof "-ERR no such module\r\n" - 1, "-ERR no such module\r\n"},
    {sizeof "-ERR init module failed\r\n" - 1, "-ERR init module failed\r\n"},
    {sizeof "-ERR uninit module failed\r\n" - 1, "-ERR uninit module failed\r\n"},
    {sizeof "-ERR module already loaded\r\n" - 1, "-ERR module already loaded\r\n"},
};

int StrToLongDouble(const char* s, size_t slen, long double* ldval) {
  char* pEnd;
  std::string t(s, slen);
  if (t.find(' ') != std::string::npos) {
    return -1;
  }
  long double d = strtold(s, &pEnd);
  if (pEnd != s + slen) {
    return -1;
  }

  if (ldval) {
    *ldval = d;
  }
  return 0;
}

size_t FormatInt(long value, UnboundedBuffer* reply) {
  if (!reply) {
    return 0;
  }

  char val[32];
  int len = snprintf(val, sizeof val, "%ld" CRLF, value);

  size_t oldSize = reply->ReadableSize();
  reply->PushData(":");
  reply->PushData(val, len);

  return reply->ReadableSize() - oldSize;
}

size_t FormatBulk(const char* str, size_t len, UnboundedBuffer* reply) {
  if (!reply) {
    return 0;
  }

  size_t oldSize = reply->ReadableSize();
  reply->PushData("$");

  char val[32];
  int tmp = snprintf(val, sizeof val - 1, "%lu" CRLF, len);
  reply->PushData(val, tmp);

  if (str && len > 0) {
    reply->PushData(str, len);
  }

  reply->PushData(CRLF, 2);

  return reply->ReadableSize() - oldSize;
}

size_t FormatBulk(const PString& str, UnboundedBuffer* reply) { return FormatBulk(str.c_str(), str.size(), reply); }

size_t PreFormatMultiBulk(size_t nBulk, UnboundedBuffer* reply) {
  if (!reply) {
    return 0;
  }

  size_t oldSize = reply->ReadableSize();
  reply->PushData("*");

  char val[32];
  int tmp = snprintf(val, sizeof val - 1, "%lu" CRLF, nBulk);
  reply->PushData(val, tmp);

  return reply->ReadableSize() - oldSize;
}

void ReplyError(PError err, UnboundedBuffer* reply) {
  if (!reply) {
    return;
  }

  const PErrorInfo& info = g_errorInfo[err];

  reply->PushData(info.errorStr, info.len);
}

size_t FormatOK(UnboundedBuffer* reply) {
  if (!reply) {
    return 0;
  }

  size_t oldSize = reply->ReadableSize();
  reply->PushData("+OK" CRLF);

  return reply->ReadableSize() - oldSize;
}

PParseResult GetIntUntilCRLF(const char*& ptr, std::size_t nBytes, int& val) {
  if (nBytes < 3) {
    return PParseResult::kWait;
  }

  std::size_t i = 0;
  bool negtive = false;
  if (ptr[0] == '-') {
    negtive = true;
    ++i;
  } else if (ptr[0] == '+') {
    ++i;
  }

  int value = 0;
  for (; i < nBytes; ++i) {
    if (isdigit(ptr[i])) {
      value *= 10;
      value += ptr[i] - '0';
    } else {
      if (ptr[i] != '\r' || (i + 1 < nBytes && ptr[i + 1] != '\n')) {
        return PParseResult::kError;
      }

      if (i + 1 == nBytes) {
        return PParseResult::kWait;
      }

      break;
    }
  }

  if (negtive) {
    value *= -1;
  }

  ptr += i;
  ptr += 2;
  val = value;
  return PParseResult::kOK;
}

std::vector<PString> SplitString(const PString& str, char seperator) {
  std::vector<PString> results;

  PString::size_type start = 0;
  PString::size_type sep = str.find(seperator);
  while (sep != PString::npos) {
    if (start < sep) {
      results.emplace_back(str.substr(start, sep - start));
    }

    start = sep + 1;
    sep = str.find(seperator, start);
  }

  if (start != str.size()) {
    results.emplace_back(str.substr(start));
  }

  return results;
}

}  // namespace pikiwidb
