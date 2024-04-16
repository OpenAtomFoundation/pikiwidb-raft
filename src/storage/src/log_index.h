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
#include <optional>
#include <shared_mutex>
#include <string_view>

#include "fmt/core.h"
#include "rocksdb/db.h"
#include "rocksdb/listener.h"
#include "rocksdb/table_properties.h"
#include "rocksdb/types.h"
#include "storage/storage_define.h"

namespace storage {

using LogIndex = int64_t;
using rocksdb::SequenceNumber;
class Redis;

constexpr int64_t kGapMax = 100;
constexpr int64_t kMaxTotalSize = 4194304;

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

struct LogIndexSeqnoPair {
  std::atomic<LogIndex> log_index = 0;
  std::atomic<SequenceNumber> seqno = 0;

  LogIndexSeqnoPair() = default;

  LogIndexSeqnoPair(LogIndex log_index, SequenceNumber seqno) : log_index(log_index), seqno(seqno) {}

  void SetAppliedLogIndex(LogIndex applied_log_index) { log_index.store(applied_log_index); }
  void SetSequenceNumber(SequenceNumber seqnum) { seqno.store(seqnum); }

  LogIndex GetAppliedLogIndex() const { return log_index.load(); }
  SequenceNumber GetSequenceNumber() const { return seqno.load(); }

  bool operator==(const LogIndexSeqnoPair &other) const { return seqno == other.seqno; }
};

class LogIndexOfColumnFamilies {
  struct LogIndexPair {
    LogIndexSeqnoPair applied_log_index;  // newest record in memtable.
    LogIndexSeqnoPair flushed_log_index;  // newest record in sst file.
  };

 public:
  // Read the largest log index of each column family from all sst files
  rocksdb::Status Init(Redis *db);

  std::tuple<int, LogIndex, SequenceNumber, int, LogIndex> GetSmallestLogIndex() const;

  // LogIndex GetSmallestFlushedLogIndex() const {
  //   return GetSmallestLogIndex([](const LogIndexPair &p) { return p.flushed_log_index.load(); });
  // }
  void SetFlushedLogIndex(size_t cf_id, LogIndex log_index, SequenceNumber seqno) {
    // 当同一个 cf 的两个 flush 同时发生的时候, 都会调用该函数, 非原子的更新两个值并的不会影响最终正确性.
    // 但是会有中间状态. 但我们只需要保证 Flushed LogIndex 是正确的, Seq 只作为是否存在数据的判断
    cf_[cf_id].flushed_log_index.log_index.store(std::max(cf_[cf_id].flushed_log_index.log_index.load(), log_index));
    cf_[cf_id].flushed_log_index.seqno.store(std::max(cf_[cf_id].flushed_log_index.seqno.load(), seqno));
  }

  void SetFlushedLogIndexGlobal(LogIndex log_index, SequenceNumber seqno) {
    SetLastMinFlushStatus(log_index, seqno);
    // 对"不包含数据的 CF" 提升 Flushed 点位.
    for (int i = 0; i < kColumnFamilyNum; i++) {
      if (cf_[i].flushed_log_index == cf_[i].applied_log_index) {
        // 只要该值能能够被提升, 那么当需要主动获取最小 Flushed 点位作为日志截断点时, 就不会收到一直不更新 CF 的影响.
        cf_[i].flushed_log_index.log_index.store(std::max(cf_[i].flushed_log_index.log_index.load(), log_index));
        cf_[i].flushed_log_index.seqno.store(std::max(cf_[i].flushed_log_index.seqno.load(), seqno));
      }
    }
  }

  bool IsApplied(size_t cf_id, LogIndex cur_log_index) const {
    return cur_log_index < cf_[cf_id].applied_log_index.log_index.load();
  }
  void Update(size_t cf_id, LogIndex cur_log_index, SequenceNumber seqno) {
    if (cf_[cf_id].flushed_log_index == cf_[cf_id].applied_log_index) {
      // 利用 max 保证最终的一致性. 但是会有中间状态.
      cf_[cf_id].flushed_log_index.log_index.store(
          std::max(cf_[cf_id].flushed_log_index.log_index.load(), last_min_flushed_logindex_.load()));
      cf_[cf_id].flushed_log_index.seqno.store(
          std::max(cf_[cf_id].flushed_log_index.seqno.load(), last_min_flushed_seqno_.load()));
    }

    cf_[cf_id].applied_log_index.log_index.store(
        std::max(cf_[cf_id].applied_log_index.log_index.load(), cur_log_index));
    cf_[cf_id].applied_log_index.seqno.store(std::max(cf_[cf_id].applied_log_index.seqno.load(), seqno));
  }
  bool IsPendingFlush() const;

  void SetLastMinFlushStatus(LogIndex min_flushed_logindex, SequenceNumber min_flushed_seqno) {
    last_min_flushed_logindex_.store(std::max(last_min_flushed_logindex_.load(), min_flushed_logindex));
    last_min_flushed_seqno_.store(std::max(last_min_flushed_seqno_.load(), min_flushed_seqno));
  }

 private:
  std::array<LogIndexPair, kColumnFamilyNum> cf_;
  std::atomic<LogIndex> last_min_flushed_logindex_ = 0;
  std::atomic<SequenceNumber> last_min_flushed_seqno_ = 0;
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
  explicit LogIndexAndSequenceCollectorPurger(std::vector<rocksdb::ColumnFamilyHandle*>* column_families, LogIndexAndSequenceCollector *collector, LogIndexOfColumnFamilies *cf)
      : column_families_(column_families), collector_(collector), cf_(cf) {}

  void OnFlushCompleted(rocksdb::DB *db, const rocksdb::FlushJobInfo &flush_job_info) override {
    // 做三件事情.
    // 1) 更新一些 CF 的 Flushed 点位.
    // 首先对当前 Flush Completed CF 的 Flushed LogIndex 设置一个可能不准确的 LogIndex(当步长 > 1) 和一个准确的 SequenceNumber.
    cf_->SetFlushedLogIndex(flush_job_info.cf_id, collector_->FindAppliedLogIndex(flush_job_info.largest_seqno),
                            flush_job_info.largest_seqno);
    LogIndex smallest_applied_log_index, smallest_flushed_log_index;
    SequenceNumber smallest_flushed_seqno;
    int smallest_applied_log_index_cf, smallest_flushed_log_index_cf;
    // 从所有 CF 中寻找 Min*数据. 在寻找的过程中跳过"没有数据的 CF", 只考虑有数据的 CF.
    // 此时返回的 Seqno 可能不是跟 Flushed LogIndex 是不匹配的, 但是可以保证是所有 CF 中最小的,
    // 但是我们对精确性要求不高, 可以忍受.
    std::tie(smallest_flushed_log_index_cf, smallest_flushed_log_index, smallest_flushed_seqno,
             smallest_applied_log_index_cf, smallest_applied_log_index) = cf_->GetSmallestLogIndex();

    // 此时 LastMinFlushed 点位可能需要更新, 同时对 "不包含数据的 CF 提升 Flushed 点位"
    cf_->SetFlushedLogIndexGlobal(smallest_flushed_log_index, smallest_flushed_seqno);

    // 2) 统计累计的数据量, 主动调用 snapshot, 保存快照.
    std::unique_lock<std::mutex> Lock(mutex_);
    total_size_ += (flush_job_info.table_properties.raw_value_size + flush_job_info.table_properties.raw_key_size);
    if (total_size_ >= kMaxTotalSize) {
      // TODO(dingxiaoshuai) 主动触发 snapshot
      total_size_ = 0;
    }
    Lock.unlock();

    // 3) 防止 collector 队列过长.
    // 周期性的对队列长度进行检查, 如果存在长时间没有 flush 的 CF, 主动 Flush .
    auto count = count_.fetch_add(1);
    auto is_flushing = manul_flushing_.load();
    if (is_flushing || count % kColumnFamilyNum != 0 || !cf_->IsPendingFlush()) {
      return;
    }
    if (!manul_flushing_.compare_exchange_strong(is_flushing, true)) {
      return;
    }
    // default: wait = true, allow_write_stall = false.
    // 同步做手动 Flush.
    rocksdb::FlushOptions flush_option;
    db->Flush(flush_option, column_families_->at(smallest_flushed_log_index_cf));
    manul_flushing_.store(false);
  }

 private:
  std::vector<rocksdb::ColumnFamilyHandle *> *column_families_;
  LogIndexAndSequenceCollector *collector_ = nullptr;
  LogIndexOfColumnFamilies *cf_ = nullptr;
  std::atomic<uint64_t> count_ = 0;
  std::atomic<bool> manul_flushing_ = false;
  std::mutex mutex_;
  int64_t total_size_ = 0;
};

}  // namespace storage