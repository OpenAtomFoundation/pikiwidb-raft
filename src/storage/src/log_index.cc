/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "log_index.h"

#include <algorithm>
#include <cinttypes>
#include <set>

#include "redis.h"

namespace storage {

static constexpr int64_t kGapMax = 1000;

rocksdb::Status storage::LogIndexOfColumnFamilies::Init(Redis *db) {
  for (int i = 0; i < cf_.size(); i++) {
    rocksdb::TablePropertiesCollection collection;
    auto s = db->GetDB()->GetPropertiesOfAllTables(db->GetColumnFamilyHandles()[i], &collection);
    if (!s.ok()) {
      return s;
    }
    auto res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(collection);
    if (res.has_value()) {
      auto log_index = res->GetAppliedLogIndex();
      auto sequence_number = res->GetSequenceNumber();
      cf_[i].applied_index.SetLogIndexSeqnoPair(log_index, sequence_number);
      cf_[i].flushed_index.SetLogIndexSeqnoPair(log_index, sequence_number);
    }
  }
  return Status::OK();
}

LogIndexOfColumnFamilies::SmallestIndexRes LogIndexOfColumnFamilies::GetSmallestLogIndex() const {
  SmallestIndexRes res;
  for (int i = 0; i < cf_.size(); i++) {
    if (cf_[i].flushed_index <= last_flush_index_ && cf_[i].flushed_index == cf_[i].applied_index) {
      continue;
    }
    auto applied_log_index = cf_[i].applied_index.GetLogIndex();
    auto flushed_log_index = cf_[i].flushed_index.GetLogIndex();
    auto flushed_seqno = cf_[i].flushed_index.GetSequenceNumber();
    if (applied_log_index < res.smallest_applied_log_index) {
      res.smallest_applied_log_index = applied_log_index;
      res.smallest_applied_log_index_cf = i;
    }
    if (flushed_log_index < res.smallest_flushed_log_index) {
      res.smallest_flushed_log_index = flushed_log_index;
      res.smallest_flushed_seqno = flushed_seqno;
      res.smallest_flushed_log_index_cf = i;
    }
  }
  return res;
}

bool LogIndexOfColumnFamilies::IsPendingFlush() const {
  std::set<int> s;
  for (int i = 0; i < kColumnFamilyNum; i++) {
    s.insert(cf_[i].applied_index.GetLogIndex());
    s.insert(cf_[i].flushed_index.GetLogIndex());
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
  // If step length > 1, log index is sampled and sacrifice precision to save memory usage.
  // It means that extra applied log may be applied again on start stage.
  if ((smallest_applied_log_index & step_length_mask_) == 0) {
    std::lock_guard gd(mutex_);
    list_.emplace_back(smallest_applied_log_index, smallest_flush_seqno);
  }
}

// TODO(longfar): find the iterator which should be deleted and erase from begin to the iterator
void LogIndexAndSequenceCollector::Purge(LogIndex smallest_applied_log_index) {
  // The reason that we use smallest applied log index of all column families instead of smallest flushed log index is
  // that the log index corresponding to the largest sequence number in the next flush must be greater than or equal to
  // the smallest applied log index at this moment.
  // So we just need to make sure that there is an element in the queue which is less than or equal to the smallest
  // applied log index to ensure that we can find a correct log index while doing next flush.
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