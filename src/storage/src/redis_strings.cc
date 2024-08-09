//  Copyright (c) 2017-present, Qihoo, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include <memory>

#include <fmt/core.h>

#include "pstd/log.h"
#include "src/base_key_format.h"
#include "src/batch.h"
#include "src/redis.h"
#include "src/scope_record_lock.h"
#include "src/scope_snapshot.h"
#include "src/strings_filter.h"
#include "storage/util.h"

namespace storage {
Status Redis::ScanStringsKeyNum(KeyInfo* key_info) {
  uint64_t keys = 0;
  uint64_t expires = 0;
  uint64_t ttl_sum = 0;
  uint64_t invalid_keys = 0;

  rocksdb::ReadOptions iterator_options;
  const rocksdb::Snapshot* snapshot;
  ScopeSnapshot ss(db_, &snapshot);
  iterator_options.snapshot = snapshot;
  iterator_options.fill_cache = false;

  int64_t curtime;
  rocksdb::Env::Default()->GetCurrentTime(&curtime);

  // Note: This is a string type and does not need to pass the column family as
  // a parameter, use the default column family
  rocksdb::Iterator* iter = db_->NewIterator(iterator_options);
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    if (!ExpectedMetaValue(DataType::kStrings, iter->value().ToString())) {
      continue;
    }
    ParsedStringsValue parsed_strings_value(iter->value());
    if (parsed_strings_value.IsStale()) {
      invalid_keys++;
    } else {
      keys++;
      if (!parsed_strings_value.IsPermanentSurvival()) {
        expires++;
        ttl_sum += parsed_strings_value.Etime() - curtime;
      }
    }
  }
  delete iter;

  key_info->keys = keys;
  key_info->expires = expires;
  key_info->avg_ttl = (expires != 0) ? ttl_sum / expires : 0;
  key_info->invalid_keys = invalid_keys;
  return Status::OK();
}

Status Redis::Append(const Slice& key, const Slice& value, int32_t* ret) {
  std::string old_value;
  *ret = 0;
  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    if (!ExpectedMetaValue(DataType::kStrings, old_value) && !IsStale(old_value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(old_value))]));
    }
    ParsedStringsValue parsed_strings_value(&old_value);
    if (parsed_strings_value.IsStale()) {
      *ret = static_cast<int32_t>(value.size());
      StringsValue strings_value(value);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    } else {
      uint64_t timestamp = parsed_strings_value.Etime();
      std::string old_user_value = parsed_strings_value.UserValue().ToString();
      std::string new_value = old_user_value + value.ToString();
      StringsValue strings_value(new_value);
      strings_value.SetEtime(timestamp);
      *ret = static_cast<int32_t>(new_value.size());
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    }
  } else if (s.IsNotFound()) {
    *ret = static_cast<int32_t>(value.size());
    StringsValue strings_value(value);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  }
  return s;
}

int GetBitCount(const unsigned char* value, int64_t bytes) {
  int bit_num = 0;
  static const unsigned char bitsinbyte[256] = {
      0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
      3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
      3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
      6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
      3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
      5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
      6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};
  for (int i = 0; i < bytes; i++) {
    bit_num += bitsinbyte[static_cast<unsigned int>(value[i])];
  }
  return bit_num;
}

Status Redis::BitCount(const Slice& key, int64_t start_offset, int64_t end_offset, int32_t* ret, bool have_range) {
  *ret = 0;
  std::string value;

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (s.ok()) {
    if (!ExpectedMetaValue(DataType::kStrings, value) && !IsStale(value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
    }

    ParsedStringsValue parsed_strings_value(&value);
    if (parsed_strings_value.IsStale()) {
      return Status::NotFound();
    } else {
      parsed_strings_value.StripSuffix();
      const auto bit_value = reinterpret_cast<const unsigned char*>(value.data());
      auto value_length = static_cast<int64_t>(value.length());
      if (have_range) {
        if (start_offset < 0) {
          start_offset = start_offset + value_length;
        }
        if (end_offset < 0) {
          end_offset = end_offset + value_length;
        }
        if (start_offset < 0) {
          start_offset = 0;
        }
        if (end_offset < 0) {
          end_offset = 0;
        }

        if (end_offset >= value_length) {
          end_offset = value_length - 1;
        }
        if (start_offset > end_offset) {
          return Status::OK();
        }
      } else {
        start_offset = 0;
        end_offset = std::max(value_length - 1, static_cast<int64_t>(0));
      }
      *ret = GetBitCount(bit_value + start_offset, end_offset - start_offset + 1);
    }
  } else {
    return s;
  }
  return Status::OK();
}

std::string BitOpOperate(BitOpType op, const std::vector<std::string>& src_values, int64_t max_len) {
  char byte;
  char output;
  auto dest_value = std::make_unique<char[]>(max_len);
  for (int64_t j = 0; j < max_len; j++) {
    if (j < static_cast<int64_t>(src_values[0].size())) {
      output = src_values[0][j];
    } else {
      output = 0;
    }
    if (op == kBitOpNot) {
      output = static_cast<char>(~output);
    }
    for (size_t i = 1; i < src_values.size(); i++) {
      if (static_cast<int64_t>(src_values[i].size()) - 1 >= j) {
        byte = src_values[i][j];
      } else {
        byte = 0;
      }
      switch (op) {
        case kBitOpNot:
          break;
        case kBitOpAnd:
          output = static_cast<char>(output & byte);
          break;
        case kBitOpOr:
          output = static_cast<char>(output | byte);
          break;
        case kBitOpXor:
          output = static_cast<char>(output ^ byte);
          break;
        case kBitOpDefault:
          break;
      }
    }
    dest_value[j] = output;
  }
  std::string dest_str(dest_value.get(), max_len);
  return dest_str;
}

Status Redis::BitOp(BitOpType op, const std::string& dest_key, const std::vector<std::string>& src_keys,
                    std::string& value_to_dest, int64_t* ret) {
  Status s;
  if (op == kBitOpNot && src_keys.size() != 1) {
    return Status::InvalidArgument("the number of source keys is not right");
  } else if (src_keys.empty()) {
    return Status::InvalidArgument("the number of source keys is not right");
  }

  int64_t max_len = 0;
  int64_t value_len = 0;
  std::vector<std::string> src_values;
  for (const auto& src_key : src_keys) {
    std::string value;
    BaseKey base_key(src_key);
    s = db_->Get(default_read_options_, base_key.Encode(), &value);
    if (s.ok()) {
      if (!ExpectedMetaValue(DataType::kStrings, value) && !IsStale(value)) {
        return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", src_key,
                                                   DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                   DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
      }

      ParsedStringsValue parsed_strings_value(&value);
      if (parsed_strings_value.IsStale()) {
        src_values.emplace_back("");
        value_len = 0;
      } else {
        parsed_strings_value.StripSuffix();
        src_values.push_back(value);
        value_len = static_cast<int64_t>(value.size());
      }
    } else if (s.IsNotFound()) {
      src_values.emplace_back("");
      value_len = 0;
    } else {
      return s;
    }
    max_len = std::max(max_len, value_len);
  }

  std::string dest_value = BitOpOperate(op, src_values, max_len);
  value_to_dest = dest_value;
  *ret = static_cast<int64_t>(dest_value.size());

  StringsValue strings_value(Slice(dest_value.c_str(), max_len));
  ScopeRecordLock l(lock_mgr_, dest_key);
  BaseKey base_dest_key(dest_key);
  return db_->Put(default_write_options_, base_dest_key.Encode(), strings_value.Encode());
}

Status Redis::Decrby(const Slice& key, int64_t value, int64_t* ret) {
  std::string old_value;
  std::string new_value;
  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    if (!ExpectedMetaValue(DataType::kStrings, old_value) && !IsStale(old_value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(old_value))]));
    }

    ParsedStringsValue parsed_strings_value(&old_value);
    if (parsed_strings_value.IsStale()) {
      *ret = -value;
      new_value = std::to_string(*ret);
      StringsValue strings_value(new_value);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    } else {
      uint64_t timestamp = parsed_strings_value.Etime();
      std::string old_user_value = parsed_strings_value.UserValue().ToString();
      char* end = nullptr;
      errno = 0;
      int64_t ival = strtoll(old_user_value.c_str(), &end, 10);
      if (errno == ERANGE || *end != 0) {
        return Status::Corruption("Value is not a integer");
      }
      if ((value >= 0 && LLONG_MIN + value > ival) || (value < 0 && LLONG_MAX + value < ival)) {
        return Status::InvalidArgument("Overflow");
      }
      *ret = ival - value;
      new_value = std::to_string(*ret);
      StringsValue strings_value(new_value);
      strings_value.SetEtime(timestamp);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    }
  } else if (s.IsNotFound()) {
    *ret = -value;
    new_value = std::to_string(*ret);
    StringsValue strings_value(new_value);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  } else {
    return s;
  }
}

Status Redis::Get(const Slice& key, std::string* value) {
  value->clear();

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), value);
  if (s.ok()) {
    if (IsStale(*value)) {
      value->clear();
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, *value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(*value))]));
    } else {
      ParsedStringsValue parsed_strings_value(value);
      parsed_strings_value.StripSuffix();
    }
  }
  return s;
}

Status Redis::GetWithTTL(const Slice& key, std::string* value, int64_t* ttl) {
  value->clear();
  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), value);
  if (s.ok()) {
    if (IsStale(*value)) {
      value->clear();
      *ttl = -2;
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, *value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(*value))]));
    } else {
      ParsedStringsValue parsed_strings_value(value);
      parsed_strings_value.StripSuffix();
      *ttl = parsed_strings_value.Etime();
      if (*ttl == 0) {
        *ttl = -1;
      } else {
        int64_t curtime;
        rocksdb::Env::Default()->GetCurrentTime(&curtime);
        *ttl = *ttl - curtime >= 0 ? *ttl - curtime : -2;
      }
    }
  } else if (s.IsNotFound()) {
    value->clear();
    *ttl = -2;
  }

  return s;
}

Status Redis::GetBit(const Slice& key, int64_t offset, int32_t* ret) {
  std::string meta_value;

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &meta_value);
  if (s.ok() || s.IsNotFound()) {
    std::string data_value;
    if (s.ok()) {
      if (IsStale(meta_value)) {
        *ret = 0;
        return Status::OK();
      } else if (!ExpectedMetaValue(DataType::kStrings, meta_value)) {
        return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                   DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                   DataTypeStrings[static_cast<int>(GetMetaValueType(meta_value))]));
      } else {
        ParsedStringsValue parsed_strings_value(&meta_value);
        data_value = parsed_strings_value.UserValue().ToString();
      }
    }
    size_t byte = offset >> 3;
    size_t bit = 7 - (offset & 0x7);
    if (byte + 1 > data_value.length()) {
      *ret = 0;
    } else {
      *ret = ((data_value[byte] & (1 << bit)) >> bit);
    }
  } else {
    return s;
  }
  return Status::OK();
}

Status Redis::Getrange(const Slice& key, int64_t start_offset, int64_t end_offset, std::string* ret) {
  *ret = "";
  std::string value;

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (s.ok()) {
    if (IsStale(value)) {
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&value);
      parsed_strings_value.StripSuffix();
      auto size = static_cast<int64_t>(value.size());
      int64_t start_t = start_offset >= 0 ? start_offset : size + start_offset;
      int64_t end_t = end_offset >= 0 ? end_offset : size + end_offset;
      if (start_t > size - 1 || (start_t != 0 && start_t > end_t) || (start_t != 0 && end_t < 0)) {
        return Status::OK();
      }
      if (start_t < 0) {
        start_t = 0;
      }
      if (end_t >= size) {
        end_t = size - 1;
      }
      if (start_t == 0 && end_t < 0) {
        end_t = 0;
      }
      *ret = value.substr(start_t, end_t - start_t + 1);
      return Status::OK();
    }
  } else {
    return s;
  }
}

Status Redis::GetrangeWithValue(const Slice& key, int64_t start_offset, int64_t end_offset, std::string* ret,
                                std::string* value, int64_t* ttl) {
  *ret = "";
  Status s = db_->Get(default_read_options_, key, value);
  if (s.ok()) {
    if (IsStale(*value)) {
      value->clear();
      *ttl = -2;
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, *value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(*value))]));
    } else {
      ParsedStringsValue parsed_strings_value(value);
      parsed_strings_value.StripSuffix();
      // get ttl
      *ttl = parsed_strings_value.Etime();
      if (*ttl == 0) {
        *ttl = -1;
      } else {
        int64_t curtime;
        rocksdb::Env::Default()->GetCurrentTime(&curtime);
        *ttl = *ttl - curtime >= 0 ? *ttl - curtime : -2;
      }

      int64_t size = value->size();
      int64_t start_t = start_offset >= 0 ? start_offset : size + start_offset;
      int64_t end_t = end_offset >= 0 ? end_offset : size + end_offset;
      if (start_t > size - 1 || (start_t != 0 && start_t > end_t) || (start_t != 0 && end_t < 0)) {
        return Status::OK();
      }
      if (start_t < 0) {
        start_t = 0;
      }
      if (end_t >= size) {
        end_t = size - 1;
      }
      if (start_t == 0 && end_t < 0) {
        end_t = 0;
      }
      *ret = value->substr(start_t, end_t - start_t + 1);
      return Status::OK();
    }
  } else if (s.IsNotFound()) {
    value->clear();
    *ttl = -2;
  }
  return s;
}

Status Redis::GetSet(const Slice& key, const Slice& value, std::string* old_value) {
  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), old_value);
  if (s.ok()) {
    if (IsStale(*old_value)) {
      *old_value = "";
    } else if (!ExpectedMetaValue(DataType::kStrings, *old_value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(*old_value))]));
    } else {
      ParsedStringsValue parsed_strings_value(old_value);
      parsed_strings_value.StripSuffix();
    }
  } else if (!s.IsNotFound()) {
    return s;
  }
  StringsValue strings_value(value);
  return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
}

Status Redis::Incrby(const Slice& key, int64_t value, int64_t* ret) {
  std::string old_value;
  std::string new_value;
  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  char buf[32] = {0};
  if (s.ok()) {
    if (IsStale(old_value)) {
      *ret = value;
      Int64ToStr(buf, 32, value);
      StringsValue strings_value(buf);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    } else if (!ExpectedMetaValue(DataType::kStrings, old_value)) {
      return Status::NotSupported(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                              DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                              DataTypeStrings[static_cast<int>(GetMetaValueType(old_value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&old_value);
      uint64_t timestamp = parsed_strings_value.Etime();
      std::string old_user_value = parsed_strings_value.UserValue().ToString();
      char* end = nullptr;
      int64_t ival = strtoll(old_user_value.c_str(), &end, 10);
      if (*end != 0) {
        return Status::Corruption("Value is not a integer");
      }
      if ((value >= 0 && LLONG_MAX - value < ival) || (value < 0 && LLONG_MIN - value > ival)) {
        return Status::InvalidArgument("Overflow");
      }
      *ret = ival + value;
      new_value = std::to_string(*ret);
      StringsValue strings_value(new_value);
      strings_value.SetEtime(timestamp);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    }
  } else if (s.IsNotFound()) {
    *ret = value;
    Int64ToStr(buf, 32, value);
    StringsValue strings_value(buf);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  } else {
    return s;
  }
}

Status Redis::Incrbyfloat(const Slice& key, const Slice& value, std::string* ret) {
  std::string old_value;
  std::string new_value;
  long double long_double_by;
  if (StrToLongDouble(value.data(), value.size(), &long_double_by) == -1) {
    return Status::Corruption("Value is not a valid float");
  }

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    if (IsStale(old_value)) {
      LongDoubleToStr(long_double_by, &new_value);
      *ret = new_value;
      StringsValue strings_value(new_value);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    } else if (!ExpectedMetaValue(DataType::kStrings, old_value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(old_value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&old_value);
      uint64_t timestamp = parsed_strings_value.Etime();
      std::string old_user_value = parsed_strings_value.UserValue().ToString();
      long double total;
      long double old_number;
      if (StrToLongDouble(old_user_value.data(), old_user_value.size(), &old_number) == -1) {
        return Status::Corruption("Value is not a valid float");
      }
      total = old_number + long_double_by;
      if (LongDoubleToStr(total, &new_value) == -1) {
        return Status::InvalidArgument("Overflow");
      }
      *ret = new_value;
      StringsValue strings_value(new_value);
      strings_value.SetEtime(timestamp);
      return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    }
  } else if (s.IsNotFound()) {
    LongDoubleToStr(long_double_by, &new_value);
    *ret = new_value;
    StringsValue strings_value(new_value);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  } else {
    return s;
  }
}

Status Redis::MSet(const std::vector<KeyValue>& kvs) {
  std::vector<std::string> keys;
  keys.reserve(kvs.size());
  for (const auto& kv : kvs) {
    keys.push_back(kv.key);
  }

  MultiScopeRecordLock ml(lock_mgr_, keys);
  auto batch = Batch::CreateBatch(this);
  for (const auto& kv : kvs) {
    BaseKey base_key(kv.key);
    StringsValue strings_value(kv.value);
    batch->Put(kMetaCF, base_key.Encode(), strings_value.Encode());
  }
  return batch->Commit();
}

Status Redis::MSetnx(const std::vector<KeyValue>& kvs, int32_t* ret) {
  Status s;
  bool exists = false;
  *ret = 0;
  std::string value;
  for (const auto& kv : kvs) {
    BaseKey base_key(kv.key);
    s = db_->Get(default_read_options_, base_key.Encode(), &value);
    if (s.ok()) {
      if (!IsStale(value)) {
        exists = true;
        break;
      }
    }
  }
  if (!exists) {
    s = MSet(kvs);
    if (s.ok()) {
      *ret = 1;
    }
  }
  return s;
}

Status Redis::Set(const Slice& key, const Slice& value) {
  StringsValue strings_value(value);
  auto batch = Batch::CreateBatch(this);
  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  batch->Put(kMetaCF, base_key.Encode(), strings_value.Encode());
  return batch->Commit();
}

Status Redis::Setxx(const Slice& key, const Slice& value, int32_t* ret, int64_t ttl) {
  bool not_found = true;
  std::string old_value;
  StringsValue strings_value(value);

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    std::string s = value.ToString();
    if (!IsStale(s)) {
      not_found = false;
    }
  } else if (!s.IsNotFound()) {
    *ret = 0;
    return s;
  }

  if (not_found) {
    *ret = 0;
    return s;
  } else {
    *ret = 1;
    if (ttl > 0) {
      strings_value.SetRelativeTimestamp(ttl);
    }
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  }
}

Status Redis::SetBit(const Slice& key, int64_t offset, int32_t on, int32_t* ret) {
  std::string meta_value;
  if (offset < 0) {
    return Status::InvalidArgument("offset < 0");
  }

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &meta_value);
  if (s.ok() || s.IsNotFound()) {
    std::string data_value;
    int32_t timestamp = 0;
    if (s.ok()) {
      if (!ExpectedMetaValue(DataType::kStrings, meta_value) && !IsStale(meta_value)) {
        return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                   DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                   DataTypeStrings[static_cast<int>(GetMetaValueType(meta_value))]));
      }
      if (!IsStale(meta_value)) {
        ParsedStringsValue parsed_strings_value(&meta_value);
        data_value = parsed_strings_value.UserValue().ToString();
        timestamp = parsed_strings_value.Etime();
      }
    }
    size_t byte = offset >> 3;
    size_t bit = 7 - (offset & 0x7);
    char byte_val;
    size_t value_lenth = data_value.length();
    if (byte + 1 > value_lenth) {
      *ret = 0;
      byte_val = 0;
    } else {
      *ret = ((data_value[byte] & (1 << bit)) >> bit);
      byte_val = data_value[byte];
    }
    if (*ret == on) {
      return Status::OK();
    }
    byte_val = static_cast<char>(byte_val & (~(1 << bit)));
    byte_val = static_cast<char>(byte_val | ((on & 0x1) << bit));
    if (byte + 1 <= value_lenth) {
      data_value.replace(byte, 1, &byte_val, 1);
    } else {
      data_value.append(byte + 1 - value_lenth - 1, 0);
      data_value.append(1, byte_val);
    }
    StringsValue strings_value(data_value);
    strings_value.SetEtime(timestamp);
    auto batch = Batch::CreateBatch(this);
    batch->Put(kMetaCF, base_key.Encode(), strings_value.Encode());
    return batch->Commit();
  } else {
    return s;
  }
}

Status Redis::Setex(const Slice& key, const Slice& value, int64_t ttl) {
  if (ttl <= 0) {
    return Status::InvalidArgument("invalid expire time");
  }
  StringsValue strings_value(value);
  auto s = strings_value.SetRelativeTimestamp(ttl);
  if (s != Status::OK()) {
    return s;
  }

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  auto batch = Batch::CreateBatch(this);
  batch->Put(kMetaCF, base_key.Encode(), strings_value.Encode());
  return batch->Commit();
}

Status Redis::Setnx(const Slice& key, const Slice& value, int32_t* ret, int64_t ttl) {
  *ret = 0;
  std::string old_value;

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    if (IsStale(old_value)) {
      StringsValue strings_value(value);
      if (ttl > 0) {
        strings_value.SetRelativeTimestamp(ttl);
      }
      s = db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
      if (s.ok()) {
        *ret = 1;
      }
    }
  } else if (s.IsNotFound()) {
    StringsValue strings_value(value);
    if (ttl > 0) {
      strings_value.SetRelativeTimestamp(ttl);
    }
    s = db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
    if (s.ok()) {
      *ret = 1;
    }
  }
  return s;
}

// 标记: redis 不存在该命令， 无法保持一致
Status Redis::Setvx(const Slice& key, const Slice& value, const Slice& new_value, int32_t* ret, int64_t ttl) {
  *ret = 0;
  std::string old_value;

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    ParsedStringsValue parsed_strings_value(&old_value);
    if (parsed_strings_value.IsStale()) {
      *ret = 0;
    } else {
      if (value.compare(parsed_strings_value.UserValue()) == 0) {
        StringsValue strings_value(new_value);
        if (ttl > 0) {
          strings_value.SetRelativeTimestamp(ttl);
        }
        s = db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
        if (!s.ok()) {
          return s;
        }
        *ret = 1;
      } else {
        *ret = -1;
      }
    }
  } else if (s.IsNotFound()) {
    *ret = 0;
  } else {
    return s;
  }
  return Status::OK();
}

// 标记: redis 不存在该命令， 无法保持一致
Status Redis::Delvx(const Slice& key, const Slice& value, int32_t* ret) {
  *ret = 0;
  std::string old_value;

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    ParsedStringsValue parsed_strings_value(&old_value);
    if (parsed_strings_value.IsStale()) {
      *ret = 0;
      return Status::NotFound("Stale");
    } else {
      if (value.compare(parsed_strings_value.UserValue()) == 0) {
        *ret = 1;
        return db_->Delete(default_write_options_, base_key.Encode());
      } else {
        *ret = -1;
      }
    }
  } else if (s.IsNotFound()) {
    *ret = 0;
  }
  return s;
}

Status Redis::Setrange(const Slice& key, int64_t start_offset, const Slice& value, int32_t* ret) {
  std::string old_value;
  std::string new_value;
  if (start_offset < 0) {
    return Status::InvalidArgument("offset < 0");
  }

  ScopeRecordLock l(lock_mgr_, key);

  BaseKey base_key(key);
  Status s = db_->Get(default_read_options_, base_key.Encode(), &old_value);
  if (s.ok()) {
    uint64_t timestamp = 0;
    if (IsStale(old_value)) {
      std::string tmp(start_offset, '\0');
      new_value = tmp.append(value.data());
      *ret = static_cast<int32_t>(new_value.length());
    } else if (!ExpectedMetaValue(DataType::kStrings, old_value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(old_value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&old_value);
      parsed_strings_value.StripSuffix();
      timestamp = parsed_strings_value.Etime();
      if (static_cast<size_t>(start_offset) > old_value.length()) {
        old_value.resize(start_offset);
        new_value = old_value.append(value.data());
      } else {
        std::string head = old_value.substr(0, start_offset);
        std::string tail;
        if ((start_offset + value.size()) < old_value.length()) {
          tail = old_value.substr(start_offset + value.size());
        }
        new_value = head + value.data() + tail;
      }
    }
    *ret = static_cast<int32_t>(new_value.length());
    StringsValue strings_value(new_value);
    strings_value.SetEtime(timestamp);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  } else if (s.IsNotFound()) {
    if (value.empty()) {  // ignore empty value
      return Status::OK();
    }
    std::string tmp(start_offset, '\0');
    new_value = tmp.append(value.data());
    *ret = static_cast<int32_t>(new_value.length());
    StringsValue strings_value(new_value);
    return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
  }
  return s;
}

Status Redis::Strlen(const Slice& key, int32_t* len) {
  std::string value;
  Status s = Get(key, &value);
  if (s.ok()) {
    *len = static_cast<int32_t>(value.size());
  } else {
    *len = 0;
  }
  return s;
}

int32_t GetBitPos(const unsigned char* s, unsigned int bytes, int bit) {
  uint64_t word = 0;
  uint64_t skip_val = 0;
  auto value = const_cast<unsigned char*>(s);
  auto l = reinterpret_cast<uint64_t*>(value);
  int pos = 0;
  if (bit == 0) {
    skip_val = std::numeric_limits<uint64_t>::max();
  } else {
    skip_val = 0;
  }
  // skip 8 bytes at one time, find the first int64 that should not be skipped
  while (bytes >= sizeof(*l)) {
    if (*l != skip_val) {
      break;
    }
    l++;
    bytes = bytes - sizeof(*l);
    pos += static_cast<int32_t>(8 * sizeof(*l));
  }
  auto c = reinterpret_cast<unsigned char*>(l);
  for (size_t j = 0; j < sizeof(*l); j++) {
    word = word << 8;
    if (bytes != 0U) {
      word = word | *c;
      c++;
      bytes--;
    }
  }
  if (bit == 1 && word == 0) {
    return -1;
  }
  // set each bit of mask to 0 except msb
  uint64_t mask = std::numeric_limits<uint64_t>::max();
  mask = mask >> 1;
  mask = ~(mask);
  while (mask != 0U) {
    if (static_cast<int>((word & mask) != 0) == bit) {
      return pos;
    }
    pos++;
    mask = mask >> 1;
  }
  return pos;
}

Status Redis::BitPos(const Slice& key, int32_t bit, int64_t* ret) {
  Status s;
  std::string value;

  BaseKey base_key(key);
  s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (s.ok()) {
    if (IsStale(value)) {
      if (bit == 1) {
        *ret = -1;
      } else if (bit == 0) {
        *ret = 0;
      }
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&value);
      parsed_strings_value.StripSuffix();
      const auto bit_value = reinterpret_cast<const unsigned char*>(value.data());
      auto value_length = static_cast<int64_t>(value.length());
      int64_t start_offset = 0;
      int64_t end_offset = std::max(value_length - 1, static_cast<int64_t>(0));
      int64_t bytes = end_offset - start_offset + 1;
      int64_t pos = GetBitPos(bit_value + start_offset, bytes, bit);
      if (pos == (8 * bytes) && bit == 0) {
        pos = -1;
      }
      if (pos != -1) {
        pos = pos + 8 * start_offset;
      }
      *ret = pos;
    }
  } else {
    return s;
  }
  return Status::OK();
}

Status Redis::BitPos(const Slice& key, int32_t bit, int64_t start_offset, int64_t* ret) {
  Status s;
  std::string value;

  BaseKey base_key(key);
  s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (s.ok()) {
    if (IsStale(value)) {
      if (bit == 1) {
        *ret = -1;
      } else if (bit == 0) {
        *ret = 0;
      }
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&value);
      parsed_strings_value.StripSuffix();
      const auto bit_value = reinterpret_cast<const unsigned char*>(value.data());
      auto value_length = static_cast<int64_t>(value.length());
      int64_t end_offset = std::max(value_length - 1, static_cast<int64_t>(0));
      if (start_offset < 0) {
        start_offset = start_offset + value_length;
      }
      if (start_offset < 0) {
        start_offset = 0;
      }
      if (start_offset > end_offset) {
        *ret = -1;
        return Status::OK();
      }
      if (start_offset > value_length - 1) {
        *ret = -1;
        return Status::OK();
      }
      int64_t bytes = end_offset - start_offset + 1;
      int64_t pos = GetBitPos(bit_value + start_offset, bytes, bit);
      if (pos == (8 * bytes) && bit == 0) {
        pos = -1;
      }
      if (pos != -1) {
        pos = pos + 8 * start_offset;
      }
      *ret = pos;
    }
  } else {
    return s;
  }
  return Status::OK();
}

Status Redis::BitPos(const Slice& key, int32_t bit, int64_t start_offset, int64_t end_offset, int64_t* ret) {
  Status s;
  std::string value;

  BaseKey base_key(key);
  s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (s.ok()) {
    if (IsStale(value)) {
      if (bit == 1) {
        *ret = -1;
      } else if (bit == 0) {
        *ret = 0;
      }
      return Status::NotFound("Stale");
    } else if (!ExpectedMetaValue(DataType::kStrings, value)) {
      return Status::InvalidArgument(fmt::format("WRONGTYPE, key: {}, expect type: {}, get type: {}", key.ToString(),
                                                 DataTypeStrings[static_cast<int>(DataType::kStrings)],
                                                 DataTypeStrings[static_cast<int>(GetMetaValueType(value))]));
    } else {
      ParsedStringsValue parsed_strings_value(&value);
      parsed_strings_value.StripSuffix();
      const auto bit_value = reinterpret_cast<const unsigned char*>(value.data());
      auto value_length = static_cast<int64_t>(value.length());
      if (start_offset < 0) {
        start_offset = start_offset + value_length;
      }
      if (start_offset < 0) {
        start_offset = 0;
      }
      if (end_offset < 0) {
        end_offset = end_offset + value_length;
      }
      // converting to int64_t just avoid warning
      if (end_offset > static_cast<int64_t>(value.length()) - 1) {
        end_offset = value_length - 1;
      }
      if (end_offset < 0) {
        end_offset = 0;
      }
      if (start_offset > end_offset) {
        *ret = -1;
        return Status::OK();
      }
      if (start_offset > value_length - 1) {
        *ret = -1;
        return Status::OK();
      }
      int64_t bytes = end_offset - start_offset + 1;
      int64_t pos = GetBitPos(bit_value + start_offset, bytes, bit);
      if (pos == (8 * bytes) && bit == 0) {
        pos = -1;
      }
      if (pos != -1) {
        pos = pos + 8 * start_offset;
      }
      *ret = pos;
    }
  } else {
    return s;
  }
  return Status::OK();
}

// TODO(wangshaoyi): timestamp uint64_t
Status Redis::PKSetexAt(const Slice& key, const Slice& value, uint64_t timestamp) {
  StringsValue strings_value(value);

  BaseKey base_key(key);
  ScopeRecordLock l(lock_mgr_, key);
  strings_value.SetEtime(uint64_t(timestamp));
  return db_->Put(default_write_options_, base_key.Encode(), strings_value.Encode());
}

Status Redis::StringsRename(const Slice& key, Redis* new_inst, const Slice& newkey) {
  std::string value;
  Status s;
  const std::vector<std::string> keys = {key.ToString(), newkey.ToString()};
  MultiScopeRecordLock ml(lock_mgr_, keys);

  BaseKey base_key(key);
  BaseKey base_newkey(newkey);
  s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (!s.ok() || !ExpectedMetaValue(DataType::kStrings, value)) {
    return s;
  }
  if (key == newkey) {
    return Status::OK();
  }

  if (IsStale(value)) {
    return Status::NotFound("Stale");
  }
  db_->Delete(default_write_options_, base_key.Encode());
  s = new_inst->GetDB()->Put(default_write_options_, base_newkey.Encode(), value);
  return s;
}

Status Redis::StringsRenamenx(const Slice& key, Redis* new_inst, const Slice& newkey) {
  std::string value;
  Status s;
  const std::vector<std::string> keys = {key.ToString(), newkey.ToString()};
  MultiScopeRecordLock ml(lock_mgr_, keys);

  BaseKey base_key(key);
  BaseKey base_newkey(newkey);
  s = db_->Get(default_read_options_, base_key.Encode(), &value);
  if (!s.ok() || !ExpectedMetaValue(DataType::kStrings, value)) {
    return s;
  }
  if (key == newkey) {
    return Status::Corruption();
  }

  if (IsStale(value)) {
    return Status::NotFound("Stale");
  }
  // check if newkey exists.
  s = new_inst->GetDB()->Get(default_read_options_, base_newkey.Encode(), &value);
  if (s.ok()) {
    if (!IsStale(value)) {
      return Status::Corruption();  // newkey already exists.
    }
  }
  db_->Delete(default_write_options_, base_key.Encode());
  s = new_inst->GetDB()->Put(default_write_options_, base_newkey.Encode(), value);

  return s;
}

void Redis::ScanStrings() {
  rocksdb::ReadOptions iterator_options;
  const rocksdb::Snapshot* snapshot;
  ScopeSnapshot ss(db_, &snapshot);
  iterator_options.snapshot = snapshot;
  iterator_options.fill_cache = false;
  auto current_time = static_cast<int32_t>(time(nullptr));
  INFO("*************** rocksdb instance: {} String Data *************** ", index_);
  auto iter = db_->NewIterator(iterator_options);
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    if (!ExpectedMetaValue(DataType::kStrings, iter->value().ToString())) {
      continue;
    }
    ParsedBaseKey parsed_strings_key(iter->key());
    ParsedStringsValue parsed_strings_value(iter->value());
    int32_t survival_time = 0;
    if (parsed_strings_value.Etime() != 0) {
      survival_time =
          parsed_strings_value.Etime() - current_time > 0 ? parsed_strings_value.Etime() - current_time : -1;
    }
    INFO("[key : {:<30}] [value : {:<30}] [timestamp : {:<10}] [version : {}] [survival_time : {}]",
         parsed_strings_key.Key().ToString(), parsed_strings_value.UserValue().ToString(), parsed_strings_value.Etime(),
         parsed_strings_value.Version(), survival_time);
  }
  delete iter;
}

Status Redis::Exists(const Slice& key) {
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        ParsedHashesMetaValue parsed_base_meta_value(&meta_value);
        if (parsed_base_meta_value.IsStale() || parsed_base_meta_value.Count() <= 0) {
          s = Status::NotFound();
        }
        break;
      }
      case DataType::kLists: {
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        if (parsed_lists_meta_value.IsStale() || parsed_lists_meta_value.Count() <= 0) {
          s = Status::NotFound();
        }
        break;
      }
      case DataType::kStrings: {
        ParsedStringsValue parsed_string_value(&meta_value);
        if (parsed_string_value.IsStale()) {
          s = Status::NotFound();
        }
      }
      default:
        break;
    }
  }
  return s;
}

Status Redis::Del(const Slice& key) {
  ScopeRecordLock l(lock_mgr_, key);
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kStrings: {
        ParsedStringsValue parsed_string_value(&meta_value);
        if (parsed_string_value.IsStale()) {
          return Status::NotFound();
        }
        return db_->Delete(default_write_options_, base_meta_key.Encode());
        break;
      }
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        if (IsStale(meta_value)) {
          return Status ::NotFound();
        }
        ParsedBaseMetaValue parsed_base_meta_value(&meta_value);
        uint64_t statistic = parsed_base_meta_value.Count();
        parsed_base_meta_value.InitialMetaValue();
        s = db_->Put(default_write_options_, handles_[kMetaCF], base_meta_key.Encode(), meta_value);
        UpdateSpecificKeyStatistics(type, key.ToString(), statistic);
        break;
      }
      case DataType::kLists: {
        if (IsStale(meta_value)) {
          return Status::NotFound();
        }
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        uint64_t statistic = parsed_lists_meta_value.Count();
        parsed_lists_meta_value.InitialMetaValue();
        s = db_->Put(default_write_options_, handles_[kMetaCF], base_meta_key.Encode(), meta_value);
        UpdateSpecificKeyStatistics(type, key.ToString(), statistic);
        break;
      }
      default:
        break;
    }
  }
  return s;
}

Status Redis::Expire(const Slice& key, int64_t timestamp) {
  ScopeRecordLock l(lock_mgr_, key);
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kStrings: {
        ParsedStringsValue parsed_strings_value(&meta_value);
        if (parsed_strings_value.IsStale()) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_strings_value.SetRelativeTimestamp(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          s = db_->Delete(default_write_options_, base_meta_key.Encode());
        }
        break;
      }
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        ParsedHashesMetaValue parsed_base_meta_value(&meta_value);
        if (parsed_base_meta_value.IsStale() || parsed_base_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_base_meta_value.SetRelativeTimestamp(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          parsed_base_meta_value.InitialMetaValue();
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        }
        break;
      }
      case DataType::kLists: {
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        if (parsed_lists_meta_value.IsStale() || parsed_lists_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_lists_meta_value.SetRelativeTimestamp(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          parsed_lists_meta_value.InitialMetaValue();
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        }
        break;
      }
      default: {
        s = Status::NotFound();
        break;
      }
    }
  }
  return s;
}

Status Redis::Expireat(const Slice& key, int64_t timestamp) {
  ScopeRecordLock l(lock_mgr_, key);
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kStrings: {
        ParsedStringsValue parsed_strings_value(&meta_value);
        if (parsed_strings_value.IsStale()) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_strings_value.SetEtime(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          s = db_->Delete(default_write_options_, base_meta_key.Encode());
        }
        break;
      }
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        ParsedHashesMetaValue parsed_base_meta_value(&meta_value);
        if (parsed_base_meta_value.IsStale() || parsed_base_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_base_meta_value.SetEtime(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          parsed_base_meta_value.InitialMetaValue();
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        }
        break;
      }
      case DataType::kLists: {
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        if (parsed_lists_meta_value.IsStale() || parsed_lists_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else if (timestamp > 0) {
          parsed_lists_meta_value.SetEtime(timestamp);
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        } else {
          parsed_lists_meta_value.InitialMetaValue();
          s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
        }
        break;
      }
      default: {
        s = Status::NotFound();
        break;
      }
    }
  }
  return s;
}

Status Redis::Persist(const Slice& key) {
  ScopeRecordLock l(lock_mgr_, key);
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kStrings: {
        ParsedStringsValue parsed_strings_value(&meta_value);
        if (parsed_strings_value.IsStale()) {
          s = Status::NotFound();
        } else {
          uint64_t expire_time = parsed_strings_value.Etime();
          if (expire_time != 0) {
            parsed_strings_value.SetEtime(0);
            s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
          } else {
            s = Status::NotFound();
          }
        }
        break;
      }
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        ParsedHashesMetaValue parsed_base_meta_value(&meta_value);
        if (parsed_base_meta_value.IsStale() || parsed_base_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else {
          uint64_t expire_time = parsed_base_meta_value.Etime();
          if (expire_time != 0) {
            parsed_base_meta_value.SetEtime(0);
            s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
          } else {
            s = Status::NotFound();
          }
        }
        break;
      }
      case DataType::kLists: {
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        if (parsed_lists_meta_value.IsStale() || parsed_lists_meta_value.Count() <= 0) {
          s = Status::NotFound();
        } else {
          uint64_t expire_time = parsed_lists_meta_value.Etime();
          if (expire_time != 0) {
            parsed_lists_meta_value.SetEtime(0);
            s = db_->Put(default_write_options_, base_meta_key.Encode(), meta_value);
          } else {
            s = Status::NotFound();
          }
        }
        break;
      }
      default: {
        s = Status::NotFound();
        break;
      }
    }
  }
  return s;
}

Status Redis::TTL(const Slice& key, int64_t* timestamp) {
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    auto type = static_cast<DataType>(static_cast<uint8_t>(meta_value[0]));
    switch (type) {
      case DataType::kStrings: {
        ParsedStringsValue parsed_strings_value(&meta_value);
        if (parsed_strings_value.IsStale()) {
          *timestamp = -2;
        } else {
          *timestamp = parsed_strings_value.Etime();
          if (*timestamp == 0) {
            *timestamp = -1;
          } else {
            int64_t curtime;
            rocksdb::Env::Default()->GetCurrentTime(&curtime);
            *timestamp = *timestamp - curtime >= 0 ? *timestamp - curtime : -2;
          }
        }
        break;
      }
      case DataType::kHashes:
      case DataType::kSets:
      case DataType::kZSets: {
        ParsedHashesMetaValue parsed_base_meta_value(&meta_value);
        if (parsed_base_meta_value.IsStale() || parsed_base_meta_value.Count() <= 0) {
          *timestamp = -2;
        } else {
          *timestamp = parsed_base_meta_value.Etime();
          if (*timestamp == 0) {
            *timestamp = -1;
          } else {
            int64_t curtime;
            rocksdb::Env::Default()->GetCurrentTime(&curtime);
            *timestamp = *timestamp - curtime >= 0 ? *timestamp - curtime : -2;
          }
        }
        break;
      }
      case DataType::kLists: {
        ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
        if (parsed_lists_meta_value.IsStale() || parsed_lists_meta_value.Count() <= 0) {
          *timestamp = -2;
        } else {
          *timestamp = parsed_lists_meta_value.Etime();
          if (*timestamp == 0) {
            *timestamp = -1;
          } else {
            int64_t curtime;
            rocksdb::Env::Default()->GetCurrentTime(&curtime);
            *timestamp = *timestamp - curtime >= 0 ? *timestamp - curtime : -2;
          }
        }
        break;
      }
      default: {
        s = Status::NotFound();
        break;
      }
    }
  } else {
    *timestamp = -2;
  }
  return s;
}

Status Redis::PKPatternMatchDel(const std::string& pattern, int32_t* ret) {
  rocksdb::ReadOptions iterator_options;
  const rocksdb::Snapshot* snapshot;
  ScopeSnapshot ss(db_, &snapshot);
  iterator_options.snapshot = snapshot;
  iterator_options.fill_cache = false;

  std::string key;
  std::string meta_value;
  int32_t total_delete = 0;
  rocksdb::Status s;
  rocksdb::WriteBatch batch;
  rocksdb::Iterator* iter = db_->NewIterator(iterator_options, handles_[kMetaCF]);
  iter->SeekToFirst();
  while (iter->Valid()) {
    auto meta_type = static_cast<enum DataType>(static_cast<uint8_t>(iter->value()[0]));
    ParsedBaseMetaKey parsed_meta_key(iter->key().ToString());
    key = iter->key().ToString();
    meta_value = iter->value().ToString();

    if (meta_type == DataType::kStrings) {
      ParsedStringsValue parsed_strings_value(&meta_value);
      if (!parsed_strings_value.IsStale() && (StringMatch(pattern.data(), pattern.size(), parsed_meta_key.Key().data(),
                                                          parsed_meta_key.Key().size(), 0) != 0)) {
        batch.Delete(key);
      }
    } else if (meta_type == DataType::kLists) {
      ParsedListsMetaValue parsed_lists_meta_value(&meta_value);
      if (!parsed_lists_meta_value.IsStale() && (parsed_lists_meta_value.Count() != 0U) &&
          (StringMatch(pattern.data(), pattern.size(), parsed_meta_key.Key().data(), parsed_meta_key.Key().size(), 0) !=
           0)) {
        parsed_lists_meta_value.InitialMetaValue();
        batch.Put(handles_[kMetaCF], iter->key(), meta_value);
      }
    } else {
      ParsedBaseMetaValue parsed_meta_value(&meta_value);
      if (!parsed_meta_value.IsStale() && (parsed_meta_value.Count() != 0) &&
          (StringMatch(pattern.data(), pattern.size(), parsed_meta_key.Key().data(), parsed_meta_key.Key().size(), 0) !=
           0)) {
        parsed_meta_value.InitialMetaValue();
        batch.Put(handles_[kMetaCF], iter->key(), meta_value);
      }
    }

    if (static_cast<size_t>(batch.Count()) >= BATCH_DELETE_LIMIT) {
      s = db_->Write(default_write_options_, &batch);
      if (s.ok()) {
        total_delete += static_cast<int32_t>(batch.Count());
        batch.Clear();
      } else {
        *ret = total_delete;
        return s;
      }
    }
    iter->Next();
  }
  if (batch.Count() != 0U) {
    s = db_->Write(default_write_options_, &batch);
    if (s.ok()) {
      total_delete += static_cast<int32_t>(batch.Count());
      batch.Clear();
    }
  }

  *ret = total_delete;
  return s;
}

Status Redis::GetType(const Slice& key, enum DataType& type) {
  BaseMetaKey base_meta_key(key);
  std::string meta_value;
  rocksdb::Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    type = static_cast<enum DataType>(static_cast<uint8_t>(meta_value[0]));
  }
  return s;
}

Status Redis::IsExist(const Slice& key) {
  std::string meta_value;
  BaseMetaKey base_meta_key(key);
  rocksdb::Status s = db_->Get(default_read_options_, handles_[kMetaCF], base_meta_key.Encode(), &meta_value);
  if (s.ok()) {
    if (IsStale(meta_value)) {
      return Status::NotFound();
    }
    return Status::OK();
  }
  return Status::NotFound();
}

}  //  namespace storage
