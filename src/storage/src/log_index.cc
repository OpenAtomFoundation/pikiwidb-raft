/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "log_index.h"

#include <algorithm>
#include <cinttypes>
#include <mutex>
#include <set>
#include <shared_mutex>

#include "redis.h"

namespace storage {

rocksdb::Status storage::LogIndexOfColumnFamilies::Init(Redis *db) {
  for (int i = 0; i < cf_.size(); i++) {
    rocksdb::TablePropertiesCollection collection;
    auto s = db->GetDB()->GetPropertiesOfAllTables(db->GetColumnFamilyHandles()[i], &collection);
    if (!s.ok()) {
      return s;
    }
    auto res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(collection);
    if (res.has_value()) {
      cf_[i].applied_log_index.log_index.store(res->GetAppliedLogIndex());
      cf_[i].applied_log_index.seqno.store(res->GetSequenceNumber());
      cf_[i].flushed_log_index.log_index.store(res->GetAppliedLogIndex());
      cf_[i].flushed_log_index.seqno.store(res->GetSequenceNumber());
    }
  }
  return Status::OK();
}

std::tuple<int, LogIndex, SequenceNumber, int, LogIndex> LogIndexOfColumnFamilies::GetSmallestLogIndex() const {
  auto smallest_applied_log_index = std::numeric_limits<LogIndex>::max();
  auto smallest_flushed_log_index = std::numeric_limits<LogIndex>::max();
  auto smallest_flushed_seqno = std::numeric_limits<SequenceNumber>::max();
  auto smallest_applied_log_index_cf = -1;
  auto smallest_flushed_log_index_cf = -1;
  for (int i = 0; i < cf_.size(); i++) {
    // 同一个 CF 以及不同的 CF 的 Flush 事件可能并发, 所有每一个 CF 的 Flushed LogIndex 和 Applied Flushed LogIndex
    // 还可能向前推进.故最后找出的 min 值可能小于真正的 min 值, 但是不影响正确性. 考虑一种情况:某一个 cf
    // 刚好把所有的数据 flush, 此时 Flushed LogIndex == Applied LogIndex, 但是不能将当前 cf 跳过. 所以还需要判断当前 cf
    // 的 Flushed seq 与 last min flushed seq 的大小.
    if (cf_[i].flushed_log_index.seqno <= last_min_flushed_seqno_.load() &&
        cf_[i].flushed_log_index == cf_[i].flushed_log_index) {
      continue;
    }
    auto applied_log_index = cf_[i].applied_log_index.log_index.load();
    auto flushed_log_index = cf_[i].flushed_log_index.log_index.load();
    auto flushed_seqno = cf_[i].flushed_log_index.seqno.load();
    // 此时会读到中间状态, 导致读到到 LogIndex 和 Seq 并不是真正的对应关系.
    if (applied_log_index < smallest_applied_log_index) {
      smallest_applied_log_index = applied_log_index;
      smallest_applied_log_index_cf = i;
    }
    if (flushed_log_index < smallest_flushed_log_index) {
      smallest_flushed_log_index = flushed_log_index;
      smallest_flushed_seqno = flushed_seqno;
      smallest_flushed_log_index_cf = i;
    }
  }
  return {smallest_flushed_log_index_cf, smallest_flushed_log_index, smallest_flushed_seqno,
          smallest_applied_log_index_cf, smallest_applied_log_index};
}

bool LogIndexOfColumnFamilies::IsPendingFlush() const {
  // assert(flushed index <= applied index)
  std::set<int> s;
  for (int i = 0; i < kColumnFamilyNum; i++) {
    s.insert(cf_[i].applied_log_index.log_index);
    s.insert(cf_[i].flushed_log_index.log_index);
  }
  assert(!s.empty());
  if (s.size() == 1) {
    return false;
  }
  auto iter_first = s.begin();
  auto iter_last = s.end();
  return *std::prev(iter_last) - *iter_first >= kGapMax;
};

std::optional<LogIndexAndSequencePair> storage::LogIndexTablePropertiesCollector::ReadStatsFromTableProps(
    const std::shared_ptr<const rocksdb::TableProperties> &table_props) {
  const auto &user_properties = table_props->user_collected_properties;
  const auto it = user_properties.find(kPropertyName.data());
  if (it == user_properties.end()) {
    return std::nullopt;
  }
  std::string s = it->second;
  LogIndex applied_log_index;
  SequenceNumber largest_seqno;
  auto res = sscanf(s.c_str(), "%" PRIi64 "/%" PRIu64 "", &applied_log_index, &largest_seqno);
  assert(res == 2);

  return LogIndexAndSequencePair(applied_log_index, largest_seqno);
}

LogIndex LogIndexAndSequenceCollector::FindAppliedLogIndex(SequenceNumber seqno) const {
  if (seqno == 0) {  // the seqno will be 0 when executing compaction
    return 0;
  }
  std::shared_lock gd(mutex_);
  if (list_.empty() || seqno < list_.front().GetSequenceNumber()) {
    return 0;
  }
  if (seqno >= list_.back().GetSequenceNumber()) {
    return list_.back().GetAppliedLogIndex();
  }

  auto it = std::lower_bound(
      list_.begin(), list_.end(), seqno,
      [](const LogIndexAndSequencePair &p, SequenceNumber tar) { return p.GetSequenceNumber() <= tar; });
  if (it->GetSequenceNumber() > seqno) {
    --it;
  }
  assert(it->GetSequenceNumber() <= seqno);
  return it->GetAppliedLogIndex();
}

void LogIndexAndSequenceCollector::Update(LogIndex smallest_applied_log_index, SequenceNumber smallest_flush_seqno) {
  /*
    If step length > 1, log index is sampled and sacrifice precision to save memory usage.
    It means that extra applied log may be applied again on start stage.
  */
  if ((smallest_applied_log_index & step_length_mask_) == 0) {
    std::lock_guard gd(mutex_);
    list_.emplace_back(smallest_applied_log_index, smallest_flush_seqno);
  }
}

// TODO(longfar): find the iterator which should be deleted and erase from begin to the iterator
void LogIndexAndSequenceCollector::Purge(LogIndex smallest_applied_log_index) {
  /*
   * The reason that we use smallest applied log index of all column families instead of smallest flushed log index is
   * that the log index corresponding to the largest sequence number in the next flush must be greater than or equal to
   * the smallest applied log index at this moment.
   * So we just need to make sure that there is an element in the queue which is less than or equal to the smallest
   * applied log index to ensure that we can find a correct log index while doing next flush.
   */
  std::lock_guard gd(mutex_);
  if (list_.size() < 2) {
    return;
  }
  auto second = std::next(list_.begin());
  while (list_.size() >= 2 && second->GetAppliedLogIndex() <= smallest_applied_log_index) {
    list_.pop_front();
    ++second;
  }
}

auto LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(
    const rocksdb::TablePropertiesCollection &collection) -> std::optional<LogIndexAndSequencePair> {
  LogIndex max_flushed_log_index{-1};
  rocksdb::SequenceNumber seqno{};
  for (const auto &[_, props] : collection) {
    auto res = LogIndexTablePropertiesCollector::ReadStatsFromTableProps(props);
    if (res.has_value() && res->GetAppliedLogIndex() > max_flushed_log_index) {
      max_flushed_log_index = res->GetAppliedLogIndex();
      seqno = res->GetSequenceNumber();
    }
  }
  return max_flushed_log_index == -1 ? std::nullopt
                                     : std::make_optional<LogIndexAndSequencePair>(max_flushed_log_index, seqno);
}

}  // namespace storage