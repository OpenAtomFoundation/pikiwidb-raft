/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string_view>

#include "fmt/core.h"
#include "gtest/gtest.h"

#include "src/log_index.h"

using namespace storage;  // NOLINT

template <typename T, T STEP>
class NumberCreator {
 public:
  explicit NumberCreator(T start = 0) : next_num_(start) {}
  auto Next() -> T { return next_num_.fetch_add(STEP); }

 private:
  std::atomic<T> next_num_;
};
using SequenceNumberCreator = NumberCreator<SequenceNumber, 2>;
using LogIndexCreator = NumberCreator<LogIndex, 1>;

TEST(LogIndexAndSequenceCollectorTest, OneStepTest) {  // NOLINT
  LogIndexAndSequenceCollector collector;
  SequenceNumberCreator seqno_creator(100);
  LogIndexCreator logidx_creator(4);
  for (int i = 0; i < 100; i++) {
    collector.Update(logidx_creator.Next(), seqno_creator.Next());
  }

  // the target seqno is smaller than the smallest seqno in the list, should return 0
  for (rocksdb::SequenceNumber seq = 0; seq < 100; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  // the target seqno is in the list' range, should return the correct idx
  for (rocksdb::SequenceNumber seq = 100; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 2 + 4);
  }
  // the target seqno is larger than the largest seqno in the list, should return the largest idx
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 103);
  }

  // if smallest flushed log index is 44 whose seqno is 180,181
  collector.Purge(44);
  for (rocksdb::SequenceNumber seq = 0; seq < 180; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  for (rocksdb::SequenceNumber seq = 180; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 2 + 4);
  }
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 103);
  }
  collector.Purge(46);  // should remove log44 and log55
  for (rocksdb::SequenceNumber seq = 0; seq < 184; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  for (rocksdb::SequenceNumber seq = 184; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 2 + 4);
  }
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 103);
  }
}

TEST(LogIndexAndSequenceCollectorTest, MutiStepTest) {  // NOLINT
  SequenceNumberCreator seqno_creator(100);
  LogIndexCreator logidx_creator(4);
  LogIndexAndSequenceCollector collector(2);  // update only when log index is multiple of 4
  for (int i = 0; i < 100; i++) {
    collector.Update(logidx_creator.Next(), seqno_creator.Next());
  }

  // the target seqno is smaller than the smallest seqno in the list, should return 0
  for (rocksdb::SequenceNumber seq = 0; seq < 100; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  // the target seqno is in the list' range, should return the correct idx
  for (rocksdb::SequenceNumber seq = 100; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 8 * 4 + 4);
  }
  // the target seqno is larger than the largest seqno in the list, should return the largest idx
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 100);
  }

  // if smallest flushed log index is 44 whose seqno is 180,181
  collector.Purge(44);
  for (rocksdb::SequenceNumber seq = 0; seq < 180; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  for (rocksdb::SequenceNumber seq = 180; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 8 * 4 + 4);
  }
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 100);
  }
  collector.Purge(45);  // should do nothing
  for (rocksdb::SequenceNumber seq = 0; seq < 180; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  for (rocksdb::SequenceNumber seq = 180; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 8 * 4 + 4);
  }
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 100);
  }
  collector.Purge(49);  // should remove the log44
  for (rocksdb::SequenceNumber seq = 0; seq < 188; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 0);
  }
  for (rocksdb::SequenceNumber seq = 188; seq < 300; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), (seq - 100) / 8 * 4 + 4);
  }
  for (rocksdb::SequenceNumber seq = 300; seq < 400; seq++) {
    EXPECT_EQ(collector.FindAppliedLogIndex(seq), 100);
  }
}

struct TimerGuard {
  TimerGuard(std::string_view name = "Test") : name_(name), start_(std::chrono::system_clock::now()) {}
  ~TimerGuard() {
    auto end = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);
    fmt::println("{} cost {}ms", name_, duration.count());
  }

  std::string_view name_;
  std::chrono::time_point<std::chrono::system_clock> start_;
};

TEST(LogIndexAndSequenceCollectorTest, FindBenchmark) {
  LogIndexAndSequenceCollector collector;
  SequenceNumberCreator seq_creator(1);
  LogIndexCreator log_creator(4);
  size_t size = 0;
  {
    for (; size < 100; size++) {
      collector.Update(log_creator.Next(), seq_creator.Next());
    }
    // There are 100 pair in the collector: 1:4, 3:5, 5:6, 7:7, 9:8,..., 199:103
    constexpr int kFindTimes = 100;
    TimerGuard timer("100 size test");
    for (int i = 0; i < kFindTimes; i++) {
      for (int n = 1; n <= 200; n++) {
        auto res = collector.FindAppliedLogIndex(n);
        ASSERT_EQ(res, (n - 1) / 2 + 4);
      }
    }
  }
  {
    for (; size < 1000; size++) {
      collector.Update(log_creator.Next(), seq_creator.Next());
    }
    // There are 1000 pair in the collector: 1:4, 3:5, 5:6, 7:7, 9:8,..., 1999:1003
    constexpr int kFindTimes = 100;
    TimerGuard timer("1000 size test");
    for (int i = 0; i < kFindTimes; i++) {
      for (int n = 1; n <= 2000; n++) {
        auto res = collector.FindAppliedLogIndex(n);
        ASSERT_EQ(res, (n - 1) / 2 + 4);
      }
    }
  }
}
