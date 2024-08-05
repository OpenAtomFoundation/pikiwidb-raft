/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

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
// #include "storage/storage.h"
#include "storage/util.h"

using namespace storage;  // NOLINT

class LogIniter {
 public:
  LogIniter() {
    logger::Init("./log_index_test.log");
    spdlog::set_level(spdlog::level::info);
  }
};
static LogIniter initer;

TEST(TablePropertyTest, SimpleTest) {
  constexpr const char* kDbPath = "./log_index_test_db";
  rocksdb::Options options;
  options.create_if_missing = true;
  LogIndexAndSequenceCollector collector;
  options.table_properties_collector_factories.push_back(
      std::make_shared<LogIndexTablePropertiesCollectorFactory>(collector));
  rocksdb::DB* db{nullptr};
  auto s = rocksdb::DB::Open(options, kDbPath, &db);
  EXPECT_TRUE(s.ok());

  std::string key = "table-property-test";
  s = db->Put(rocksdb::WriteOptions(), key, key);
  EXPECT_TRUE(s.ok());
  std::string res;
  s = db->Get(rocksdb::ReadOptions(), key, &res);
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(key, res);
  collector.Update(233333, db->GetLatestSequenceNumber());
  db->Flush(rocksdb::FlushOptions());

  rocksdb::TablePropertiesCollection properties;
  s = db->GetPropertiesOfAllTables(&properties);
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(properties.size() == 1);
  for (auto& [name, prop] : properties) {
    const auto& collector = prop->user_collected_properties;
    auto it = collector.find(static_cast<std::string>(LogIndexTablePropertiesCollector::kPropertyName));
    EXPECT_NE(it, collector.cend());
    EXPECT_EQ(it->second, "233333/" + std::to_string(db->GetLatestSequenceNumber()));
  }

  db->Close();
  DeleteFiles(kDbPath);
}

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

class LogIndexTest : public ::testing::Test {
 public:
  LogIndexTest()
      : log_queue_([this](const pikiwidb::Binlog& log, LogIndex log_idx) { return db_.OnBinlogWrite(log, log_idx); }) {
    options_.options.create_if_missing = true;
    options_.db_instance_num = 1;
    options_.raft_timeout_s = 10000;
    options_.append_log_function = [this](const pikiwidb::Binlog& log, std::promise<rocksdb::Status>&& promise) {
      log_queue_.AppendLog(log, std::move(promise));
    };
    options_.do_snapshot_function = [](int64_t log_index, bool sync) {};
  }
  ~LogIndexTest() override { DeleteFiles(db_path_.c_str()); }

  void SetUp() override {
    if (access(db_path_.c_str(), F_OK) == 0) {
      std::filesystem::remove_all(db_path_.c_str());
    }
    mkdir(db_path_.c_str(), 0755);
    auto s = db_.Open(options_, db_path_);
    ASSERT_TRUE(s.ok());
  }

  std::string db_path_{"./test_db/log_index_test"};
  StorageOptions options_;
  Storage db_;
  uint32_t test_times_ = 100;
  std::string key_ = "log-index-test";
  std::string field_prefix_ = "field";
  std::string value_prefix_ = "value";
  rocksdb::WriteOptions write_options_;
  rocksdb::ReadOptions read_options_;
  LogQueue log_queue_;

  auto CreateRandomKey(int i, size_t length) -> std::string {
    auto res = CreateRandomFieldValue(i, length);
    res.append(key_);
    return res;
  }
  static auto CreateRandomFieldValue(int i, size_t length) -> std::string {
    std::mt19937 gen(i);
    std::string str(length, 0);
    for (int i = 0; i < length; i++) {
      str[i] = chars[gen() % (sizeof(chars) / sizeof(char))];
    }
    return str;
  }
  constexpr static char chars[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
                                   'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
                                   'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                                   'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
};

TEST_F(LogIndexTest, DoNothing) {}

TEST_F(LogIndexTest, SimpleTest) {  // NOLINT
  auto& redis = db_.GetDBInstance(key_);
  auto add_kvs = [&](int start, int end) {
    for (int i = start; i < end; i++) {
      auto key = CreateRandomKey(i, 256);
      auto fv = CreateRandomFieldValue(i, 512);
      int32_t res{};
      auto s = redis->HSet(key, fv, fv, &res);
      ASSERT_TRUE(s.ok());
      ASSERT_EQ(1, res);

      std::string get_res;
      s = redis->HGet(key, fv, &get_res);
      ASSERT_TRUE(s.ok());
      ASSERT_EQ(fv, get_res);
    }
  };
  auto flushdb = [&]() {
    auto s = redis->GetDB()->Flush(rocksdb::FlushOptions(), redis->GetColumnFamilyHandles()[kMetaCF]);
    ASSERT_TRUE(s.ok());
    s = redis->GetDB()->Flush(rocksdb::FlushOptions(), redis->GetColumnFamilyHandles()[kHashesDataCF]);
    ASSERT_TRUE(s.ok());
  };

  // one key test
  {
    add_kvs(0, 1);
    flushdb();

    rocksdb::TablePropertiesCollection properties;
    auto s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kMetaCF], &properties);
    ASSERT_TRUE(s.ok());
    ASSERT_TRUE(properties.size() == 1);
    auto res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
    EXPECT_TRUE(res.has_value());
    assert(res.has_value());
    EXPECT_EQ(res->GetAppliedLogIndex(), 1);
    EXPECT_EQ(res->GetSequenceNumber(), 1);

    properties.clear();
    s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kHashesDataCF], &properties);
    ASSERT_TRUE(s.ok());
    ASSERT_TRUE(properties.size() == 1);
    res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
    EXPECT_TRUE(res.has_value());
    assert(res.has_value());
    EXPECT_EQ(res->GetAppliedLogIndex(), 1);
    EXPECT_EQ(res->GetSequenceNumber(), 2);
  }

  // more keys
  {
    add_kvs(1, 10000);
    flushdb();

    rocksdb::TablePropertiesCollection properties;
    auto s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kMetaCF], &properties);
    ASSERT_TRUE(s.ok());
    auto res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
    EXPECT_TRUE(res.has_value());
    assert(res.has_value());
    EXPECT_EQ(res->GetAppliedLogIndex(), 10000);
    EXPECT_EQ(res->GetSequenceNumber(), 19999);

    properties.clear();
    s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kMetaCF], &properties);
    ASSERT_TRUE(s.ok());
    res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
    EXPECT_TRUE(res.has_value());
    assert(res.has_value());
    EXPECT_EQ(res->GetAppliedLogIndex(), 10000);
    EXPECT_EQ(res->GetSequenceNumber(), 20000);
  }

  // more flush
  {
    for (int i = 1; i < 20; i++) {
      fmt::println("==================i={} start==========================", i);
      auto start = i * 10000;
      auto end = start + 10000;

      add_kvs(start, end);
      flushdb();
      // sleep(1);

      {
        rocksdb::TablePropertiesCollection properties;
        auto s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kMetaCF], &properties);
        s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kHashesDataCF], &properties);
        std::vector<rocksdb::LiveFileMetaData> metas;
        redis->GetDB()->GetLiveFilesMetaData(&metas);
        for (const auto& meta : metas) {
          auto file = meta.directory + meta.name;
          if (!properties.contains(file)) {
            fmt::println("{}: L{}, {}, not contains", file, meta.level, meta.column_family_name);
            continue;
          }
          auto res = LogIndexTablePropertiesCollector::ReadStatsFromTableProps(properties.at(file));
          assert(res.has_value());
          fmt::println("{}: L{}, {}, logidx={}", file, meta.level, meta.column_family_name, res->GetAppliedLogIndex());
        }
      }

      rocksdb::TablePropertiesCollection properties;
      auto s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kMetaCF], &properties);
      ASSERT_TRUE(s.ok());
      auto res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
      EXPECT_TRUE(res.has_value());
      assert(res.has_value());
      EXPECT_EQ(res->GetAppliedLogIndex(), end);
      EXPECT_EQ(res->GetSequenceNumber(), end * 2 - 1);

      properties.clear();
      s = redis->GetDB()->GetPropertiesOfAllTables(redis->GetColumnFamilyHandles()[kHashesDataCF], &properties);
      ASSERT_TRUE(s.ok());
      res = LogIndexTablePropertiesCollector::GetLargestLogIndexFromTableCollection(properties);
      EXPECT_TRUE(res.has_value());
      assert(res.has_value());
      EXPECT_EQ(res->GetAppliedLogIndex(), end);
      EXPECT_EQ(res->GetSequenceNumber(), end * 2);
    }
  }
}
