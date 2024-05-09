/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <optional>
#include <shared_mutex>
#include <string_view>

#include "fmt/core.h"
#include "rocksdb/db.h"
#include "rocksdb/listener.h"
#include "rocksdb/table_properties.h"
#include "rocksdb/types.h"

#include "storage/storage.h"

namespace storage {

using LogIndex = int64_t;
using rocksdb::SequenceNumber;
class Redis;

class LogIndexAndSequencePair {
 public:
  LogIndexAndSequencePair(LogIndex applied_log_index, SequenceNumber seqno)
      : applied_log_index_(applied_log_index), seqno_(seqno) {}

  void SetAppliedLogIndex(LogIndex applied_log_index) { applied_log_index_ = applied_log_index; }
  void SetSequenceNumber(SequenceNumber seqno) { seqno_ = seqno; }

  LogIndex GetAppliedLogIndex() const { return applied_log_index_; }
  SequenceNumber GetSequenceNumber() const { return seqno_; }

 private:
  LogIndex applied_log_index_ = 0;
  SequenceNumber seqno_ = 0;
};

class LogIndexAndSequenceCollector {
 public:
  explicit LogIndexAndSequenceCollector(uint8_t step_length_bit = 0) { step_length_mask_ = (1 << step_length_bit) - 1; }

  // find the index of log which contain seqno or before it
  LogIndex FindAppliedLogIndex(SequenceNumber seqno) const;

  // if there's a new pair, add it to list; otherwise, do nothing
  void Update(LogIndex smallest_applied_log_index, SequenceNumber smallest_flush_seqno);

  // purge out dated log index after memtable flushed.
  void Purge(LogIndex smallest_applied_log_index);

  // Is manual flushing required?
  bool IsFlushPending() const { return GetSize() >= max_gap.load(); }

  // for gtest
  uint64_t GetSize() const {
    std::shared_lock<std::shared_mutex> share_lock;
    return list_.size();
  }

  std::deque<LogIndexAndSequencePair> &GetList() {
    std::shared_lock<std::shared_mutex> share_lock;
    return list_;
  }

 public:
  static std::atomic_int64_t max_gap;

 private:
  uint64_t step_length_mask_ = 0;
  mutable std::shared_mutex mutex_;
  std::deque<LogIndexAndSequencePair> list_;
};

class LogIndexTablePropertiesCollector : public rocksdb::TablePropertiesCollector {
 public:
  static constexpr std::string_view kPropertyName = "LargestLogIndex/LargestSequenceNumber";

  explicit LogIndexTablePropertiesCollector(const LogIndexAndSequenceCollector &collector) : collector_(collector) {}

  rocksdb::Status AddUserKey(const rocksdb::Slice &key, const rocksdb::Slice &value, rocksdb::EntryType type,
                             SequenceNumber seq, uint64_t file_size) override {
    largest_seqno_ = std::max(largest_seqno_, seq);
    return rocksdb::Status::OK();
  }
  rocksdb::Status Finish(rocksdb::UserCollectedProperties *properties) override {
    properties->insert(Materialize());
    return rocksdb::Status::OK();
  }
  const char *Name() const override { return "LogIndexTablePropertiesCollector"; }
  rocksdb::UserCollectedProperties GetReadableProperties() const override {
    return rocksdb::UserCollectedProperties{Materialize()};
  }

  static std::optional<LogIndexAndSequencePair> ReadStatsFromTableProps(
      const std::shared_ptr<const rocksdb::TableProperties> &table_props);

  static auto GetLargestLogIndexFromTableCollection(const rocksdb::TablePropertiesCollection &collection)
      -> std::optional<LogIndexAndSequencePair>;

 private:
  std::pair<std::string, std::string> Materialize() const {
    if (-1 == cache_) {
      cache_ = collector_.FindAppliedLogIndex(largest_seqno_);
    }
    return std::make_pair(static_cast<std::string>(kPropertyName), fmt::format("{}/{}", cache_, largest_seqno_));
  }

 private:
  const LogIndexAndSequenceCollector &collector_;
  SequenceNumber largest_seqno_ = 0;
  mutable LogIndex cache_{-1};
};

class LogIndexTablePropertiesCollectorFactory : public rocksdb::TablePropertiesCollectorFactory {
 public:
  explicit LogIndexTablePropertiesCollectorFactory(const LogIndexAndSequenceCollector &collector)
      : collector_(collector) {}
  ~LogIndexTablePropertiesCollectorFactory() override = default;

  rocksdb::TablePropertiesCollector *CreateTablePropertiesCollector(
      [[maybe_unused]] rocksdb::TablePropertiesCollectorFactory::Context context) override {
    return new LogIndexTablePropertiesCollector(collector_);
  }
  const char *Name() const override { return "LogIndexTablePropertiesCollectorFactory"; }

 private:
  const LogIndexAndSequenceCollector &collector_;
};

class LogIndexAndSequenceCollectorPurger : public rocksdb::EventListener {
 public:
  explicit LogIndexAndSequenceCollectorPurger(std::vector<rocksdb::ColumnFamilyHandle *> *column_families,
                                              LogIndexAndSequenceCollector *collector, PointPairOfRocksDB *cf)
      : column_families_(column_families), collector_(collector), cf_(cf) {}

  void OnFlushCompleted(rocksdb::DB *db, const rocksdb::FlushJobInfo &flush_job_info) override;

 private:
  std::vector<rocksdb::ColumnFamilyHandle *> *column_families_ = nullptr;
  LogIndexAndSequenceCollector *collector_ = nullptr;
  PointPairOfRocksDB *cf_ = nullptr;
  std::atomic<size_t> manul_flushing_cf_ = -1;
};

}  // namespace storage