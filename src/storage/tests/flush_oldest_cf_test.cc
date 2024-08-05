/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "gtest/gtest.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "gtest/gtest.h"
#include "rocksdb/db.h"
#include "rocksdb/listener.h"
#include "rocksdb/metadata.h"
#include "rocksdb/options.h"

#include "pstd/log.h"
#include "pstd/thread_pool.h"
#include "src/log_index.h"
#include "src/redis.h"
#include "storage/storage.h"
#include "storage/util.h"

class LogIniter {
 public:
  LogIniter() {
    logger::Init("./flush_oldest_cf_test.log");
    spdlog::set_level(spdlog::level::info);
  }
};

LogIniter log_initer;

using LogIndex = int64_t;

class LogQueue : public pstd::noncopyable {
 public:
  using WriteCallback = std::function<rocksdb::Status(const pikiwidb::Binlog&, LogIndex idx)>;

  explicit LogQueue(WriteCallback&& cb) : write_cb_(std::move(cb)) { consumer_.SetMaxIdleThread(1); }

  void AppendLog(const pikiwidb::Binlog& log, std::promise<rocksdb::Status>&& promise) {
    auto task = [&] {
      auto idx = next_log_idx_.fetch_add(1);
      auto s = write_cb_(log, idx);
      promise.set_value(s);
    };
    consumer_.ExecuteTask(std::move(task));
  }

 private:
  WriteCallback write_cb_ = nullptr;
  pstd::ThreadPool consumer_;
  std::atomic<LogIndex> next_log_idx_{1};
};

class FlushOldestCFTest : public ::testing::Test {
 public:
  FlushOldestCFTest()
      : log_queue_([this](const pikiwidb::Binlog& log, LogIndex log_idx) { return db_.OnBinlogWrite(log, log_idx); }) {
    options_.options.create_if_missing = true;
    options_.options.max_background_jobs = 10;
    options_.db_instance_num = 1;
    options_.raft_timeout_s = 9000000;
    options_.append_log_function = [this](const pikiwidb::Binlog& log, std::promise<rocksdb::Status>&& promise) {
      log_queue_.AppendLog(log, std::move(promise));
    };
    options_.do_snapshot_function = [](int64_t log_index, bool sync) {};
    options_.max_gap = 15;
    write_options_.disableWAL = true;
  }

  ~FlushOldestCFTest() { rocksdb::DestroyDB(db_path_, rocksdb::Options()); }

  void SetUp() override {
    if (access(db_path_.c_str(), F_OK) == 0) {
      std::filesystem::remove_all(db_path_.c_str());
    }
    mkdir(db_path_.c_str(), 0755);
    auto s = db_.Open(options_, db_path_);
    ASSERT_TRUE(s.ok());
  }

  std::string db_path_{"./test_db/flush_oldest_cf_test"};
  storage::StorageOptions options_;
  storage::Storage db_;
  uint32_t test_times_ = 100;
  std::string key_ = "flush-oldest-cf-test";
  std::string key_prefix = "key_";
  std::string field_prefix_ = "field_";
  std::string value_prefix_ = "value_";
  rocksdb::WriteOptions write_options_;
  rocksdb::ReadOptions read_options_;
  LogQueue log_queue_;
};

TEST_F(FlushOldestCFTest, SimpleTest) {
  const auto& rocksdb = db_.GetDBInstance(key_);

  auto add_kvs = [&](int start, int end) {
    for (int i = start; i < end; i++) {
      auto key = key_prefix + std::to_string(i);
      auto v = value_prefix_ + std::to_string(i);
      auto s = rocksdb->Set(key, v);
      ASSERT_TRUE(s.ok());
    }
  };

  auto add_hash = [&](int start, int end) {
    for (int i = start; i < end; i++) {
      auto key = key_prefix + std::to_string(i);
      auto v = value_prefix_ + std::to_string(i);
      auto f = field_prefix_ + std::to_string(i);
      int32_t res{};
      auto s = rocksdb->HSet(key, v, f, &res);
      ASSERT_TRUE(s.ok());
      ASSERT_EQ(res, 1);
    }
  };

  auto flush_cf = [&](size_t cf) {
    auto s = rocksdb->GetDB()->Flush(rocksdb::FlushOptions(), rocksdb->GetColumnFamilyHandles()[cf]);
    ASSERT_TRUE(s.ok());
  };

  {
    //  type    kv            kv
    // entry  [1:1] -> ... [10:10]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                        0              0
    add_kvs(0, 10);
    auto& last_flush_index = rocksdb->GetLogIndexOfColumnFamilies().GetLastFlushIndex();
    ASSERT_EQ(last_flush_index.log_index.load(), 0);
    ASSERT_EQ(last_flush_index.seqno.load(), 0);

    auto& cf_0_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_0_status.flushed_index.log_index, 0);
    ASSERT_EQ(cf_0_status.flushed_index.seqno, 0);
    ASSERT_EQ(cf_0_status.applied_index.log_index, 10);
    ASSERT_EQ(cf_0_status.applied_index.seqno, 10);

    auto [smallest_applied_log_index_cf, smallest_applied_log_index, smallest_flushed_log_index_cf,
          smallest_flushed_log_index, smallest_flushed_seqno] =
        rocksdb->GetLogIndexOfColumnFamilies().GetSmallestLogIndex(-1);

    ASSERT_EQ(smallest_flushed_log_index, 0);
    ASSERT_EQ(smallest_flushed_seqno, 0);
    ASSERT_EQ(smallest_applied_log_index, 10);
    auto size = rocksdb->GetCollector().GetSize();
    ASSERT_EQ(size, 10);
  }

  {
    //  type     kv            kv         hash        hash                 hash
    // entry   [1:1] -> ... [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    //  1           0                    0                        30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0               0
    add_hash(10, 30);
    auto& last_flush_index = rocksdb->GetLogIndexOfColumnFamilies().GetLastFlushIndex();
    ASSERT_EQ(last_flush_index.log_index.load(), 0);
    ASSERT_EQ(last_flush_index.seqno.load(), 0);

    auto& cf_1_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_1_status.flushed_index.log_index, 0);
    ASSERT_EQ(cf_1_status.flushed_index.seqno, 0);
    ASSERT_EQ(cf_1_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_1_status.applied_index.seqno, 49);

    auto& cf_2_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kHashesDataCF);
    ASSERT_EQ(cf_2_status.flushed_index.log_index, 0);
    ASSERT_EQ(cf_2_status.flushed_index.seqno, 0);
    ASSERT_EQ(cf_2_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_2_status.applied_index.seqno, 50);

    auto [smallest_applied_log_index_cf, smallest_applied_log_index, smallest_flushed_log_index_cf,
          smallest_flushed_log_index, smallest_flushed_seqno] =
        rocksdb->GetLogIndexOfColumnFamilies().GetSmallestLogIndex(-1);

    ASSERT_EQ(smallest_flushed_log_index, 0);
    ASSERT_EQ(smallest_flushed_seqno, 0);
    ASSERT_EQ(smallest_applied_log_index, 10);

    auto size = rocksdb->GetCollector().GetSize();
    ASSERT_EQ(size, 30);

    auto is_pending_flush = rocksdb->GetCollector().IsFlushPending();
    ASSERT_TRUE(is_pending_flush);
  }

  {
    //  type    kv            kv         hash        hash                 hash
    // entry  [1:1] -> ... [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    auto cur_par = rocksdb->GetCollector().GetList().begin();
    auto logindex = 1;
    auto seq = 1;
    for (int i = 1; i <= 10; i++) {
      ASSERT_EQ(cur_par->GetAppliedLogIndex(), logindex);
      ASSERT_EQ(cur_par->GetSequenceNumber(), seq);
      cur_par = std::next(cur_par);
      logindex++;
      seq++;
    }

    for (int i = 11; i <= 30; i++) {
      ASSERT_EQ(cur_par->GetAppliedLogIndex(), logindex);
      ASSERT_EQ(cur_par->GetSequenceNumber(), seq);
      seq += 2;
      logindex++;
      cur_par = std::next(cur_par);
    }
  }

  {
    //  type       kv            kv         hash        hash                 hash
    // entry     [1:1] -> ... [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    //  1           0                    0                        30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0               0

    auto gap = rocksdb->GetLogIndexOfColumnFamilies().GetPendingFlushGap();
    ASSERT_EQ(gap, 30);
    flush_cf(1);
    sleep(5);  // sleep flush complete.
    // 1) 根据 cf 1 的 latest SequenceNumber = 49 查到对应的 log index 为 30. 设置 cf 1 的 flushed_log_index 和
    // flushed_sequence_number 为 30 49.
    //
    //  type     kv            kv         hash        hash                 hash
    // entry   [1:1] -> ... [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    //  1           30                   49                       30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0               0

    // 2) 查找到此时的 smallest_applied_log_index_cf = 0 smallest_applied_log_index = 10
    // smallest_flushed_log_index_cf = 0
    //               smallest_flushed_log_index = 0 smallest_flushed_seqno = 0
    // 根据 smallest_applied_log_index = 10 在队列长度 >= 2 的前提下, 持续删除 log_index < 10 的条目.
    //
    //  type      kv         hash        hash                 hash
    // entry   [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    //  1           30                   49                       30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0               0

    // 3) 根据 smallest_flushed_log_index_cf =  0 smallest_flushed_log_index = 0 smallest_flushed_seqno = 0
    // 设置 last_flush_index 为 0, 0
    //
    //  type      kv         hash        hash                 hash
    // entry   [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           0                    0                        10                   10
    //  1           30                   49                       30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0              0

    // 4) 检测到队列中 logindex 的最大差值超过阈值, 触发 smallest_flushed_log_index_cf flush . 该 case 中对应 cf 为 0.
    //  根据 cf 0 的 latest SequenceNumber = 10 查到对应的 log index 为 10. 设置 cf 0 的 flushed_log_index 和
    //  flushed_sequence_number 为 10 10.
    //
    //  type      kv         hash        hash                 hash
    // entry   [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           10                   10                        10                   10
    //  1           30                   49                       30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0              0

    // 5) 查找到此时的 smallest_applied_log_index_cf = 0 smallest_applied_log_index = 10
    // smallest_flushed_log_index_cf = 2 smallest_flushed_log_index = 0 smallest_flushed_seqno = 0
    // 根据 smallest_applied_log_index = 10 在队列长度 >= 2 的前提下, 删除 log_index < 10 的条目, 不变.
    //
    //  type      kv         hash        hash                 hash
    // entry   [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           10                   10                        10                   10
    //  1           30                   49                       30                   49
    //  2           0                    0                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0              0

    // 6) 检测到队列中 logindex 的最大差值超过阈值, 触发 smallest_flushed_log_index_cf flush . 该 case 中对应 cf 为 2.
    //  根据 cf 2 的 latest SequenceNumber = 50 查到对应的 log index 为 30. 设置 cf 2 的 flushed_log_index 和
    //  flushed_sequence_number 为 30 50.
    //
    //  type      kv         hash        hash                 hash
    // entry   [10:10]  -> [11:11]  -> [12:13]  -> ...  -> [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           10                   10                        10                   10
    //  1           30                   49                       30                   49
    //  2           30                   50                        30                   50
    // other        0                    0                        0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0              0

    // 7) 查找到此时的 smallest_applied_log_index_cf = 2 smallest_applied_log_index = 30
    // smallest_flushed_log_index_cf = 2 smallest_flushed_log_index = 30 smallest_flushed_seqno = 50
    // 根据 smallest_applied_log_index = 30 在队列长度 >= 2 的前提下, 删除 log_index < 50 的条目.
    //
    //  type     hash
    // entry   [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           10                   10                      10                   10
    //  1           30                   49                      30                   49
    //  2           30                   50                      30                   50
    // other        0                    0                       0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       0              0

    // 8) 根据 smallest_flushed_log_index_cf = 2 smallest_flushed_log_index = 30 smallest_flushed_seqno = 50
    // 设置 last_flush_index 为 30, 50.
    //
    //  type     hash
    // entry   [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           10                   10                      10                   10
    //  1           30                   49                      30                   49
    //  2           30                   50                      30                   50
    // other        0                    0                       0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       30              50

    // 9) 当设置 last_flush_index 为 30, 50 时, 会同时拉高没有数据的 cf 的 flushed_index, 该 case 为 cf 0, cf 1,
    // 将 cf 0 的 flushed_index 从 10 10 提高为 30 50.
    // 将 cf 1 的 flushed index 从 30 49 提升到 30 50.
    // 其他没有写入的 cf flushed index 从 0 0 提升到 30 50.
    //
    //  type     hash
    // entry   [30:49]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           30                   50                      10                   10
    //  1           30                   50                      30                   49
    //  2           30                   50                      30                   50
    // other        30                   50                       0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       30              50

    // 9) 检测到队列长度未超过阈值, 结束 flush.
    auto after_flush_size = rocksdb->GetCollector().GetSize();
    ASSERT_EQ(after_flush_size, 1);

    auto& cf_0_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_0_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_0_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_0_status.applied_index.log_index, 10);
    ASSERT_EQ(cf_0_status.applied_index.seqno, 10);

    auto& cf_1_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_1_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_1_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_1_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_1_status.applied_index.seqno, 49);

    auto& cf_2_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kHashesDataCF);
    ASSERT_EQ(cf_2_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_2_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_2_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_2_status.applied_index.seqno, 50);

    auto& last_flush_index = rocksdb->GetLogIndexOfColumnFamilies().GetLastFlushIndex();
    ASSERT_EQ(last_flush_index.log_index.load(), 30);
    ASSERT_EQ(last_flush_index.seqno.load(), 50);

    auto& cf_3_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_3_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_3_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_3_status.applied_index.log_index, 0);
    ASSERT_EQ(cf_3_status.applied_index.seqno, 0);
  }

  {
    add_kvs(30, 35);
    //  type     hash    ->   kv    ->  ...  ->  kv
    // entry   [30:49]     [31:51]             [35:55]
    //
    //  cf   flushed_log_index  flushed_sequence_number  applied_log_index  applied_sequence_number
    //  0           30                   50                      35                   55
    //  1           30                   50                      30                   49
    //  2           30                   50                      30                   50
    // other        30                   50                       0                    0
    //
    // last_flush_index   log_index    sequencenumber
    //                       30              50
    auto& last_flush_index = rocksdb->GetLogIndexOfColumnFamilies().GetLastFlushIndex();
    ASSERT_EQ(last_flush_index.log_index.load(), 30);
    ASSERT_EQ(last_flush_index.seqno.load(), 50);

    auto& cf_0_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_0_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_0_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_0_status.applied_index.log_index, 35);
    ASSERT_EQ(cf_0_status.applied_index.seqno, 55);

    auto& cf_1_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_1_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_1_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_1_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_1_status.applied_index.seqno, 49);

    auto& cf_2_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kHashesDataCF);
    ASSERT_EQ(cf_2_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_2_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_2_status.applied_index.log_index, 30);
    ASSERT_EQ(cf_2_status.applied_index.seqno, 50);

    auto& cf_3_status = rocksdb->GetLogIndexOfColumnFamilies().GetCFStatus(storage::kMetaCF);
    ASSERT_EQ(cf_3_status.flushed_index.log_index, 30);
    ASSERT_EQ(cf_3_status.flushed_index.seqno, 50);
    ASSERT_EQ(cf_3_status.applied_index.log_index, 0);
    ASSERT_EQ(cf_3_status.applied_index.seqno, 0);

    auto [smallest_applied_log_index_cf, smallest_applied_log_index, smallest_flushed_log_index_cf,
          smallest_flushed_log_index, smallest_flushed_seqno] =
        rocksdb->GetLogIndexOfColumnFamilies().GetSmallestLogIndex(-1);

    // 除了 cf 0 之外, 其余的 cf 都没有未持久化数据, 所以不在我们统计范围之内.
    ASSERT_EQ(smallest_applied_log_index_cf, 0);
    ASSERT_EQ(smallest_applied_log_index, 35);

    ASSERT_EQ(smallest_flushed_log_index_cf, 0);
    ASSERT_EQ(smallest_flushed_log_index, 30);
    ASSERT_EQ(smallest_flushed_seqno, 50);

    auto size = rocksdb->GetCollector().GetSize();
    ASSERT_EQ(size, 6);

    auto is_pending_flush = rocksdb->GetCollector().IsFlushPending();
    ASSERT_TRUE(!is_pending_flush);
  }
};
