// Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <zlib.h>
#include <ctime>
#include <thread>
#include <unordered_set>

#include "cache/config.h"
#include "cache/redisCache.h"
#include "cmd_zset.h"
#include "pcache.h"
#include "pcache_load_thread.h"
#include "pstd/log.h"
#include "store.h"

namespace pikiwidb {

#define EXTEND_CACHE_SIZE(N) (N * 12 / 10)
using rocksdb::Status;

PCache::PCache(int zset_cache_start_direction, int zset_cache_field_num_per_key)
    : cache_status_(PCACHE_STATUS_NONE),
      cache_num_(0),
      zset_cache_start_direction_(zset_cache_start_direction),
      zset_cache_field_num_per_key_(EXTEND_CACHE_SIZE(zset_cache_field_num_per_key)) {
  cache_load_thread_ = std::make_unique<PCacheLoadThread>(zset_cache_start_direction_, zset_cache_field_num_per_key_);
  cache_load_thread_->StartThread();
}

PCache::~PCache() {
  {
    std::lock_guard l(rwlock_);
    DestroyWithoutLock();
  }
}

Status PCache::Init(uint32_t cache_num, cache::CacheConfig *cache_cfg) {
  std::lock_guard l(rwlock_);

  if (nullptr == cache_cfg) {
    return Status::Corruption("invalid arguments !!!");
  }
  return InitWithoutLock(cache_num, cache_cfg);
}

void PCache::ProcessCronTask(void) {
  std::lock_guard l(rwlock_);
  for (uint32_t i = 0; i < caches_.size(); ++i) {
    std::unique_lock lm(*cache_mutexs_[i]);
    caches_[i]->ActiveExpireCycle();
  }
}

Status PCache::Reset(uint32_t cache_num, cache::CacheConfig *cache_cfg) {
  std::lock_guard l(rwlock_);

  DestroyWithoutLock();
  return InitWithoutLock(cache_num, cache_cfg);
}

void PCache::ResetConfig(cache::CacheConfig *cache_cfg) {
  std::lock_guard l(rwlock_);
  zset_cache_start_direction_ = cache_cfg->zset_cache_start_direction;
  zset_cache_field_num_per_key_ = EXTEND_CACHE_SIZE(cache_cfg->zset_cache_field_num_per_key);
  WARN("zset-cache-start-direction: {} , zset_cache_field_num_per_key: {} ", zset_cache_start_direction_,
       zset_cache_field_num_per_key_);
  cache::RedisCache::SetConfig(cache_cfg);
}

void PCache::Destroy(void) {
  std::lock_guard l(rwlock_);
  DestroyWithoutLock();
}

void PCache::SetCacheStatus(int status) { cache_status_ = status; }

int PCache::CacheStatus(void) { return cache_status_; }

/*-----------------------------------------------------------------------------
 * Normal Commands
 *----------------------------------------------------------------------------*/
void PCache::Info(CacheInfo &info) {
  info.clear();
  std::unique_lock l(rwlock_);
  info.status = cache_status_;
  info.cache_num = cache_num_;
  info.used_memory = cache::RedisCache::GetUsedMemory();
  // info.async_load_keys_num = cache_load_thread_->AsyncLoadKeysNum();
  //   info.waitting_load_keys_num = cache_load_thread_->WaittingLoadKeysNum();
  cache::RedisCache::GetHitAndMissNum(&info.hits, &info.misses);
  for (uint32_t i = 0; i < caches_.size(); ++i) {
    std::lock_guard lm(*cache_mutexs_[i]);
    info.keys_num += caches_[i]->DbSize();
  }
}

bool PCache::Exists(std::string &key) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Exists(key);
}

void PCache::FlushCache(void) {
  std::lock_guard l(rwlock_);
  for (uint32_t i = 0; i < caches_.size(); ++i) {
    std::lock_guard lm(*cache_mutexs_[i]);
    caches_[i]->FlushCache();
  }
}

Status PCache::Del(const std::vector<std::string> &keys) {
  rocksdb::Status s;
  for (const auto &key : keys) {
    int cache_index = CacheIndex(key);
    std::lock_guard lm(*cache_mutexs_[cache_index]);
    s = caches_[cache_index]->Del(key);
  }
  return s;
}

Status PCache::Expire(std::string &key, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Expire(key, ttl);
}

Status PCache::Expireat(std::string &key, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Expireat(key, ttl);
}

Status PCache::TTL(std::string &key, int64_t *ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->TTL(key, ttl);
}

int64_t PCache::TTL(std::string &key) {
  Status s;
  int64_t timestamp = 0;
  int cache_index = CacheIndex(key);
  s = caches_[cache_index]->TTL(key, &timestamp);
  if (s.ok()) {
    return timestamp;
  } else if (s.IsNotFound()) {
    return PCache_KEY_NOT_FOUND;
  } else if (!s.IsNotFound()) {
    return PCache_TTL_FAILED;
  }
  return timestamp;
}

Status PCache::Persist(std::string &key) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Persist(key);
}

Status PCache::Type(std::string &key, std::string *value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Type(key, value);
}

Status PCache::RandomKey(std::string *key) {
  Status s;
  srand((unsigned)time(nullptr));
  int cache_index = rand() % caches_.size();
  for (unsigned int i = 0; i < caches_.size(); ++i) {
    cache_index = (cache_index + i) % caches_.size();

    std::lock_guard lm(*cache_mutexs_[cache_index]);
    s = caches_[cache_index]->RandomKey(key);
    if (s.ok()) {
      break;
    }
  }
  return s;
}

/*-----------------------------------------------------------------------------
 * String Commands
 *----------------------------------------------------------------------------*/
Status PCache::Set(std::string &key, std::string &value, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Set(key, value, ttl);
}

Status PCache::Setnx(std::string &key, std::string &value, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Setnx(key, value, ttl);
}

Status PCache::SetnxWithoutTTL(std::string &key, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SetnxWithoutTTL(key, value);
}

Status PCache::Setxx(std::string &key, std::string &value, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Setxx(key, value, ttl);
}

Status PCache::SetxxWithoutTTL(std::string &key, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SetxxWithoutTTL(key, value);
}

Status PCache::Get(std::string &key, std::string *value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Get(key, value);
}

Status PCache::MSet(const std::vector<storage::KeyValue> &kvs) {
  for (const auto &item : kvs) {
    auto [key, value] = item;
    int cache_index = CacheIndex(key);
    std::lock_guard lm(*cache_mutexs_[cache_index]);
    return caches_[cache_index]->SetxxWithoutTTL(key, value);
  }
  return Status::OK();
}

Status PCache::MGet(const std::vector<std::string> &keys, std::vector<storage::ValueStatus> *vss) {
  vss->resize(keys.size());
  rocksdb::Status ret;
  for (int i = 0; i < keys.size(); ++i) {
    int cache_index = CacheIndex(keys[i]);
    std::lock_guard lm(*cache_mutexs_[cache_index]);
    auto s = caches_[cache_index]->Get(keys[i], &(*vss)[i].value);
    (*vss)[i].status = s;
    if (!s.ok()) {
      ret = s;
    }
  }
  return ret;
}

Status PCache::Incrxx(std::string &key) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->Incr(key);
  }
  return Status::NotFound("key not exist");
}

Status PCache::Decrxx(std::string &key) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->Decr(key);
  }
  return Status::NotFound("key not exist");
}

Status PCache::IncrByxx(std::string &key, uint64_t incr) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->IncrBy(key, incr);
  }
  return Status::NotFound("key not exist");
}

Status PCache::DecrByxx(std::string &key, uint64_t incr) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->DecrBy(key, incr);
  }
  return Status::NotFound("key not exist");
}

Status PCache::Incrbyfloatxx(std::string &key, long double incr) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->Incrbyfloat(key, incr);
  }
  return Status::NotFound("key not exist");
}

Status PCache::Appendxx(std::string &key, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->Append(key, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::GetRange(std::string &key, int64_t start, int64_t end, std::string *value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->GetRange(key, start, end, value);
}

Status PCache::SetRangexx(std::string &key, int64_t start, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->SetRange(key, start, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::Strlen(std::string &key, int32_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->Strlen(key, len);
}

/*-----------------------------------------------------------------------------
 * Hash Commands
 *----------------------------------------------------------------------------*/
Status PCache::HDel(std::string &key, std::vector<std::string> &fields) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HDel(key, fields);
}

Status PCache::HSet(std::string &key, std::string &field, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HSet(key, field, value);
}

Status PCache::HSetIfKeyExist(std::string &key, std::string &field, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->HSet(key, field, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::HSetIfKeyExistAndFieldNotExist(std::string &key, std::string &field, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->HSetnx(key, field, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::HMSet(std::string &key, std::vector<storage::FieldValue> &fvs) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HMSet(key, fvs);
}

Status PCache::HMSetnx(std::string &key, std::vector<storage::FieldValue> &fvs, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->HMSet(key, fvs);
    caches_[cache_index]->Expire(key, ttl);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::HMSetnxWithoutTTL(std::string &key, std::vector<storage::FieldValue> &fvs) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->HMSet(key, fvs);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::HMSetxx(std::string &key, std::vector<storage::FieldValue> &fvs) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->HMSet(key, fvs);
  } else {
    return Status::NotFound("key not exist");
  }
}

Status PCache::HGet(std::string &key, std::string &field, std::string *value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HGet(key, field, value);
}

Status PCache::HMGet(std::string &key, std::vector<std::string> &fields, std::vector<storage::ValueStatus> *vss) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HMGet(key, fields, vss);
}

Status PCache::HGetall(std::string &key, std::vector<storage::FieldValue> *fvs) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HGetall(key, fvs);
}

Status PCache::HKeys(std::string &key, std::vector<std::string> *fields) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HKeys(key, fields);
}

Status PCache::HVals(std::string &key, std::vector<std::string> *values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HVals(key, values);
}

Status PCache::HExists(std::string &key, std::string &field) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HExists(key, field);
}

Status PCache::HIncrbyxx(std::string &key, std::string &field, int64_t value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->HIncrby(key, field, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::HIncrbyfloatxx(std::string &key, std::string &field, long double value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->HIncrbyfloat(key, field, value);
  }
  return Status::NotFound("key not exist");
}

Status PCache::HLen(std::string &key, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HLen(key, len);
}

Status PCache::HStrlen(std::string &key, std::string &field, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->HStrlen(key, field, len);
}

/*-----------------------------------------------------------------------------
 * List Commands
 *----------------------------------------------------------------------------*/
Status PCache::LIndex(std::string &key, int64_t index, std::string *element) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LIndex(key, index, element);
}

Status PCache::LInsert(std::string &key, storage::BeforeOrAfter &before_or_after, std::string &pivot,
                       std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LInsert(key, before_or_after, pivot, value);
}

Status PCache::LLen(std::string &key, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LLen(key, len);
}

Status PCache::LPop(std::string &key, std::string *element) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LPop(key, element);
}

Status PCache::LPush(std::string &key, std::vector<std::string> &values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LPush(key, values);
}

Status PCache::LPushx(std::string &key, std::vector<std::string> &values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LPushx(key, values);
}

Status PCache::LRange(std::string &key, int64_t start, int64_t stop, std::vector<std::string> *values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LRange(key, start, stop, values);
}

Status PCache::LRem(std::string &key, int64_t count, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LRem(key, count, value);
}

Status PCache::LSet(std::string &key, int64_t index, std::string &value) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LSet(key, index, value);
}

Status PCache::LTrim(std::string &key, int64_t start, int64_t stop) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->LTrim(key, start, stop);
}

Status PCache::RPop(std::string &key, std::string *element) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->RPop(key, element);
}

Status PCache::RPush(std::string &key, std::vector<std::string> &values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->RPush(key, values);
}

Status PCache::RPushx(std::string &key, std::vector<std::string> &values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->RPushx(key, values);
}

Status PCache::RPushnx(std::string &key, std::vector<std::string> &values, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->RPush(key, values);
    caches_[cache_index]->Expire(key, ttl);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::RPushnxWithoutTTL(std::string &key, std::vector<std::string> &values) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->RPush(key, values);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

// /*-----------------------------------------------------------------------------
//  * Set Commands
//  *----------------------------------------------------------------------------*/
Status PCache::SAdd(std::string &key, std::vector<std::string> &members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SAdd(key, members);
}

Status PCache::SAddIfKeyExist(std::string &key, std::vector<std::string> &members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (caches_[cache_index]->Exists(key)) {
    return caches_[cache_index]->SAdd(key, members);
  }
  return Status::NotFound("key not exist");
}

Status PCache::SAddnx(std::string &key, std::vector<std::string> &members, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->SAdd(key, members);
    caches_[cache_index]->Expire(key, ttl);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::SAddnxWithoutTTL(std::string &key, std::vector<std::string> &members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->SAdd(key, members);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::SCard(std::string &key, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SCard(key, len);
}

Status PCache::SIsmember(std::string &key, std::string &member) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SIsmember(key, member);
}

Status PCache::SMembers(std::string &key, std::vector<std::string> *members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SMembers(key, members);
}

Status PCache::SRem(std::string &key, std::vector<std::string> &members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SRem(key, members);
}

Status PCache::SRandmember(std::string &key, int64_t count, std::vector<std::string> *members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  return caches_[cache_index]->SRandmember(key, count, members);
}

// /*-----------------------------------------------------------------------------
//  * ZSet Commands
//  *----------------------------------------------------------------------------*/
// Status PCache::ZAdd(std::string& key, std::vector<storage::ScoreMember> &score_members) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->ZAdd(key, score_members);
// }

void PCache::GetMinMaxScore(std::vector<storage::ScoreMember> &score_members, double &min, double &max) {
  if (score_members.empty()) {
    return;
  }
  min = max = score_members.front().score;
  for (auto &item : score_members) {
    if (item.score < min) {
      min = item.score;
    }
    if (item.score > max) {
      max = item.score;
    }
  }
}

bool PCache::GetCacheMinMaxSM(cache::RedisCache *cache_obj, std::string &key, storage::ScoreMember &min_m,
                              storage::ScoreMember &max_m) {
  if (cache_obj) {
    std::vector<storage::ScoreMember> score_members;
    auto s = cache_obj->ZRange(key, 0, 0, &score_members);
    if (!s.ok() || score_members.empty()) {
      return false;
    }
    min_m = score_members.front();
    score_members.clear();

    s = cache_obj->ZRange(key, -1, -1, &score_members);
    if (!s.ok() || score_members.empty()) {
      return false;
    }
    max_m = score_members.front();
    return true;
  }
  return false;
}

// used
Status PCache::ZAddIfKeyExistInCache(std::string &key, std::vector<storage::ScoreMember> &score_members,
                                     PClient *client) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  int db_len = 0;
  PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZCard(key, &db_len);
  if (db_len > zset_cache_field_num_per_key_) {
    return cache_obj->Del(key);
  }
  if (cache_obj->Exists(key)) {
    return cache_obj->ZAdd(key, score_members);
  } else {
    return Status::NotFound("key not exist");
  }
}

Status PCache::ZAddIfKeyExist(std::string &key, std::vector<storage::ScoreMember> &score_members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];
  Status s;
  if (cache_obj->Exists(key)) {
    uint64_t cache_len = 0;
    cache_obj->ZCard(key, &cache_len);
    if (cache_len + score_members.size() <= (unsigned long)zset_cache_field_num_per_key_) {
      return cache_obj->ZAdd(key, score_members);
    }

    std::unordered_set<std::string> unique;
    std::list<storage::ScoreMember> filtered_score_members;
    for (auto it = score_members.rbegin(); it != score_members.rend(); ++it) {
      if (unique.find(it->member) == unique.end()) {
        unique.insert(it->member);
        filtered_score_members.push_front(*it);
      }
    }
    std::vector<storage::ScoreMember> new_score_members;
    for (auto &item : filtered_score_members) {
      new_score_members.push_back(std::move(item));
    }

    // when add element, you should keep score order between cache and DB.
    double min_score = storage::ZSET_SCORE_MIN;
    double max_score = storage::ZSET_SCORE_MAX;
    GetMinMaxScore(new_score_members, min_score, max_score);

    storage::ScoreMember cache_min_sm;
    storage::ScoreMember cache_max_sm;
    if (!GetCacheMinMaxSM(cache_obj, key, cache_min_sm, cache_max_sm)) {
      return Status::NotFound("key not exist");
    }
    auto cache_min_score = cache_min_sm.score;
    auto cache_max_score = cache_max_sm.score;
    if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
      if (max_score < cache_max_score) {
        cache_obj->ZAdd(key, new_score_members);
      } else {
        std::vector<storage::ScoreMember> score_members_can_add;
        std::vector<std::string> members_need_remove;
        bool left_close = false;
        for (auto &item : new_score_members) {
          if (item.score == cache_max_score) {
            left_close = true;
            score_members_can_add.push_back(item);
            continue;
          }
          if (item.score < cache_max_score) {
            score_members_can_add.push_back(item);
          } else {
            members_need_remove.push_back(item.member);
          }
        }
        if (!score_members_can_add.empty()) {
          cache_obj->ZAdd(key, score_members_can_add);
          std::string cache_max_score_str = left_close ? "" : "(" + std::to_string(cache_max_score);
          std::string max_str = "+inf";
          cache_obj->ZRemrangebyscore(key, cache_max_score_str, max_str);
        }
        if (!members_need_remove.empty()) {
          cache_obj->ZRem(key, members_need_remove);
        }
      }
    } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
      if (min_score > cache_min_score) {
        cache_obj->ZAdd(key, new_score_members);
      } else {
        std::vector<storage::ScoreMember> score_members_can_add;
        std::vector<std::string> members_need_remove;
        bool right_close = false;
        for (auto &item : new_score_members) {
          if (item.score == cache_min_score) {
            right_close = true;
            score_members_can_add.push_back(item);
            continue;
          }
          if (item.score > cache_min_score) {
            score_members_can_add.push_back(item);
          } else {
            members_need_remove.push_back(item.member);
          }
        }
        if (!score_members_can_add.empty()) {
          cache_obj->ZAdd(key, score_members_can_add);
          std::string cache_min_score_str = right_close ? "" : "(" + std::to_string(cache_min_score);
          std::string min_str = "-inf";
          cache_obj->ZRemrangebyscore(key, min_str, cache_min_score_str);
        }
        if (!members_need_remove.empty()) {
          cache_obj->ZRem(key, members_need_remove);
        }
      }
    }

    return CleanCacheKeyIfNeeded(cache_obj, key);
  } else {
    return Status::NotFound("key not exist");
  }
}

Status PCache::CleanCacheKeyIfNeeded(cache::RedisCache *cache_obj, std::string &key) {
  uint64_t cache_len = 0;
  cache_obj->ZCard(key, &cache_len);
  if (cache_len > (unsigned long)zset_cache_field_num_per_key_) {
    long start = 0;
    long stop = 0;
    if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
      start = -cache_len + zset_cache_field_num_per_key_;
      stop = -1;
    } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
      start = 0;
      stop = cache_len - zset_cache_field_num_per_key_ - 1;
    }
    auto min = std::to_string(start);
    auto max = std::to_string(stop);
    cache_obj->ZRemrangebyrank(key, min, max);
  }
  return Status::OK();
}

Status PCache::ZAddnx(std::string &key, std::vector<storage::ScoreMember> &score_members, int64_t ttl) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->ZAdd(key, score_members);
    caches_[cache_index]->Expire(key, ttl);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

Status PCache::ZAddnxWithoutTTL(std::string &key, std::vector<storage::ScoreMember> &score_members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  if (!caches_[cache_index]->Exists(key)) {
    caches_[cache_index]->ZAdd(key, score_members);
    return Status::OK();
  } else {
    return Status::NotFound("key exist");
  }
}

// used
Status PCache::ZCard(std::string &key, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZCard(key, len);
  } else {
    return Status::NotFound("key not exist");
  }
}

Status PCache::CacheZCard(std::string &key, uint64_t *len) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);

  return caches_[cache_index]->ZCard(key, len);
}

RangeStatus PCache::CheckCacheRangeByScore(uint64_t cache_len, double cache_min, double cache_max, double min,
                                           double max, bool left_close, bool right_close) {
  bool cache_full = (cache_len == (unsigned long)zset_cache_field_num_per_key_);

  if (cache_full) {
    if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
      bool ret = (max < cache_max);
      if (ret) {
        if (max < cache_min) {
          return RangeStatus::RangeError;
        } else {
          return RangeStatus::RangeHit;
        }
      } else {
        return RangeStatus::RangeMiss;
      }
    } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
      bool ret = min > cache_min;
      if (ret) {
        if (min > cache_max) {
          return RangeStatus::RangeError;
        } else {
          return RangeStatus::RangeHit;
        }
      } else {
        return RangeStatus::RangeMiss;
      }
    } else {
      return RangeStatus::RangeError;
    }
  } else {
    if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
      bool ret = right_close ? max < cache_max : max <= cache_max;
      if (ret) {
        if (max < cache_min) {
          return RangeStatus::RangeError;
        } else {
          return RangeStatus::RangeHit;
        }
      } else {
        return RangeStatus::RangeMiss;
      }
    } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
      bool ret = left_close ? min > cache_min : min >= cache_min;
      if (ret) {
        if (min > cache_max) {
          return RangeStatus::RangeError;
        } else {
          return RangeStatus::RangeHit;
        }
      } else {
        return RangeStatus::RangeMiss;
      }
    } else {
      return RangeStatus::RangeError;
    }
  }
}

// Status PCache::ZCount(std::string& key, std::string &min, std::string &max, uint64_t *len, ZCountCmd *cmd) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   auto cache_obj = caches_[cache_index];
//   uint64_t cache_len = 0;
//   cache_obj->ZCard(key, &cache_len);
//   if (cache_len <= 0) {
//     return Status::NotFound("key not in cache");
//   } else {
//     storage::ScoreMember cache_min_sm;
//     storage::ScoreMember cache_max_sm;
//     if (!GetCacheMinMaxSM(cache_obj, key, cache_min_sm, cache_max_sm)) {
//       return Status::NotFound("key not exist");
//     }
//     auto cache_min_score = cache_min_sm.score;
//     auto cache_max_score = cache_max_sm.score;

//     if (RangeStatus::RangeHit == CheckCacheRangeByScore(cache_len, cache_min_score, cache_max_score, cmd->MinScore(),
//                                                         cmd->MaxScore(), cmd->LeftClose(), cmd->RightClose())) {
//       auto s = cache_obj->ZCount(key, min, max, len);
//       return s;
//     } else {
//       return Status::NotFound("key not in cache");
//     }
//   }
// }

// Status PCache::ZIncrby(std::string& key, std::string& member, double increment) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->ZIncrby(key, member, increment);
// }

bool PCache::ReloadCacheKeyIfNeeded(cache::RedisCache *cache_obj, std::string &key, int mem_len, int db_len,
                                    PClient *client) {
  if (mem_len == -1) {
    uint64_t cache_len = 0;
    cache_obj->ZCard(key, &cache_len);
    mem_len = cache_len;
  }
  if (db_len == -1) {
    db_len = 0;
    PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZCard(key, &db_len);
    if (!db_len) {
      return false;
    }
  }
  if (db_len < zset_cache_field_num_per_key_) {
    if (mem_len * 2 < db_len) {
      cache_obj->Del(key);
      PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
      return true;
    } else {
      return false;
    }
  } else {
    if (zset_cache_field_num_per_key_ && mem_len * 2 < zset_cache_field_num_per_key_) {
      cache_obj->Del(key);
      PushKeyToAsyncLoadQueue(KEY_TYPE_ZSET, key, client);
      return true;
    } else {
      return false;
    }
  }
}

// used
Status PCache::ZIncrbyIfKeyExist(std::string &key, std::string &member, double increment, double score,
                                 PClient *client) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];
  int db_len = 0;
  PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->ZCard(key, &db_len);
  if (db_len > zset_cache_field_num_per_key_) {
    return cache_obj->Del(key);
  }
  if (cache_obj->Exists(key)) {
    std::vector<storage::ScoreMember> score_member = {{score, member}};
    return cache_obj->ZAdd(key, score_member);
  } else {
    return Status::NotFound("key not exist");
  }
}

// RangeStatus PCache::CheckCacheRange(int32_t cache_len, int32_t db_len, int64_t start, int64_t stop, int64_t
// &out_start,
//                                        int64_t &out_stop) {
//   out_start = start >= 0 ? start : db_len + start;
//   out_stop = stop >= 0 ? stop : db_len + stop;
//   out_start = out_start <= 0 ? 0 : out_start;
//   out_stop = out_stop >= db_len ? db_len - 1 : out_stop;
//   if (out_start > out_stop || out_start >= db_len || out_stop < 0) {
//     return RangeStatus::RangeError;
//   } else {
//     if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
//       if (out_start < cache_len && out_stop < cache_len) {
//         return RangeStatus::RangeHit;
//       } else {
//         return RangeStatus::RangeMiss;
//       }
//     } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
//       if (out_start >= db_len - cache_len && out_stop >= db_len - cache_len) {
//         out_start = out_start - (db_len - cache_len);
//         out_stop = out_stop - (db_len - cache_len);
//         return RangeStatus::RangeHit;
//       } else {
//         return RangeStatus::RangeMiss;
//       }
//     } else {
//       return RangeStatus::RangeError;
//     }
//   }
// }

// RangeStatus PCache::CheckCacheRevRange(int32_t cache_len, int32_t db_len, int64_t start, int64_t stop, int64_t
// &out_start,
//                                           int64_t &out_stop) {
//   int64_t start_index = stop >= 0 ? db_len - stop - 1 : -stop - 1;
//   int64_t stop_index = start >= 0 ? db_len - start - 1 : -start - 1;
//   start_index = start_index <= 0 ? 0 : start_index;
//   stop_index = stop_index >= db_len ? db_len - 1 : stop_index;
//   if (start_index > stop_index || start_index >= db_len || stop_index < 0) {
//     return RangeStatus::RangeError;
//   } else {
//     if (zset_cache_start_direction_ == cache::CACHE_START_FROM_BEGIN) {
//       if (start_index < cache_len && stop_index < cache_len) {
//         // cache reverse index
//         out_start = cache_len - stop_index - 1;
//         out_stop = cache_len - start_index - 1;

//         return RangeStatus::RangeHit;
//       } else {
//         return RangeStatus::RangeMiss;
//       }
//     } else if (zset_cache_start_direction_ == cache::CACHE_START_FROM_END) {
//       if (start_index >= db_len - cache_len && stop_index >= db_len - cache_len) {
//         int cache_start = start_index - (db_len - cache_len);
//         int cache_stop = stop_index - (db_len - cache_len);
//         out_start = cache_len - cache_stop - 1;
//         out_stop = cache_len - cache_start - 1;
//         return RangeStatus::RangeHit;
//       } else {
//         return RangeStatus::RangeMiss;
//       }
//     } else {
//       return RangeStatus::RangeError;
//     }
//   }
// }

// Status PCache::ZRange(std::string& key, int64_t start, int64_t stop, std::vector<storage::ScoreMember>
// *score_members,
//                          const std::shared_ptr<DB>& db) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);

//   auto cache_obj = caches_[cache_index];
//   auto db_obj = db->storage();
//   Status s;
//   if (cache_obj->Exists(key)) {
//     uint64_t cache_len = 0;
//     cache_obj->ZCard(key, &cache_len);
//     int32_t db_len = 0;
//     db_obj->ZCard(key, &db_len);
//     int64_t out_start = 0;
//     int64_t out_stop = 0;
//     RangeStatus rs = CheckCacheRange(cache_len, db_len, start, stop, out_start, out_stop);
//     if (rs == RangeStatus::RangeHit) {
//       return cache_obj->ZRange(key, out_start, out_stop, score_members);
//     } else if (rs == RangeStatus::RangeMiss) {
//       ReloadCacheKeyIfNeeded(cache_obj, key, cache_len, db_len, client);
//       return Status::NotFound("key not in cache");
//     } else if (rs == RangeStatus::RangeError) {
//       return Status::NotFound("error range");
//     } else {
//       return Status::Corruption("unknown error");
//     }
//   } else {
//     return Status::NotFound("key not in cache");
//   }
// }

// used
Status PCache::ZRangebyscore(std::string &key, std::string &min, std::string &max,
                             std::vector<storage::ScoreMember> *score_members, ZRangebyscoreCmd *cmd) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRangebyscore(key, min, max, score_members, cmd->Offset(), cmd->Count());
  } else {
    return Status::NotFound("key not in cache");
  }
}

// used
Status PCache::ZRank(std::string &key, std::string &member, int64_t *rank) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRank(key, member, rank);
  } else {
    return Status::NotFound("key not exist");
  }
}

// used
Status PCache::ZRem(std::string &key, std::vector<std::string> &members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRem(key, members);
  } else {
    return Status::NotFound("key not exist");
  }
}

// used
Status PCache::ZRemrangebyrank(std::string &key, int32_t start_index, int32_t stop_index) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    auto cache_min_str = std::to_string(start_index);
    auto cache_max_str = std::to_string(stop_index);
    return cache_obj->ZRemrangebyrank(key, cache_min_str, cache_max_str);
  } else {
    return Status::NotFound("key not exist");
  }
}

// used
Status PCache::ZRemrangebyscore(std::string &key, std::string &min, std::string &max) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];
  if (cache_obj->Exists(key)) {
    return cache_obj->ZRemrangebyscore(key, min, max);
  } else {
    return Status::NotFound("key not exist");
  }
}

// Status PCache::ZRevrange(std::string& key, int64_t start, int64_t stop, std::vector<storage::ScoreMember>
// *score_members,
//                             const std::shared_ptr<DB>& db) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);

//   auto cache_obj = caches_[cache_index];
//   auto db_obj = db->storage();
//   Status s;
//   if (cache_obj->Exists(key)) {
//     uint64_t cache_len = 0;
//     cache_obj->ZCard(key, &cache_len);
//     int32_t db_len = 0;
//     db_obj->ZCard(key, &db_len);
//     int64_t out_start = 0;
//     int64_t out_stop = 0;
//     RangeStatus rs = CheckCacheRevRange(cache_len, db_len, start, stop, out_start, out_stop);
//     if (rs == RangeStatus::RangeHit) {
//       return cache_obj->ZRevrange(key, out_start, out_stop, score_members);
//     } else if (rs == RangeStatus::RangeMiss) {
//       ReloadCacheKeyIfNeeded(cache_obj, key, cache_len, db_len, client);
//       return Status::NotFound("key not in cache");
//     } else if (rs == RangeStatus::RangeError) {
//       return Status::NotFound("error revrange");
//     } else {
//       return Status::Corruption("unknown error");
//     }
//   } else {
//     return Status::NotFound("key not in cache");
//   }
// }

// used
Status PCache::ZRevrangebyscore(std::string &key, std::string &min, std::string &max,
                                std::vector<storage::ScoreMember> *score_members, ZRevrangebyscoreCmd *cmd) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRevrangebyscore(key, min, max, score_members, cmd->Offset(), cmd->Count());
  } else {
    return Status::NotFound("key not in cache");
  }
}

// bool PCache::CacheSizeEqsDB(std::string& key, const std::shared_ptr<DB>& db) {
//   int32_t db_len = 0;
//   db->storage()->ZCard(key, &db_len);

//   std::lock_guard l(rwlock_);
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   uint64_t cache_len = 0;
//   caches_[cache_index]->ZCard(key, &cache_len);
//   return (db_len == (int32_t)cache_len) && cache_len;
// }

// used
Status PCache::ZRevrangebylex(std::string &key, std::string &min, std::string &max, std::vector<std::string> *members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRevrangebylex(key, min, max, members);
  } else {
    return Status::NotFound("key not exist");
  }
}

// used
Status PCache::ZRevrank(std::string &key, std::string &member, int64_t *rank) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRevrank(key, member, rank);
  } else {
    return Status::NotFound("key not exist");
  }
}

Status PCache::ZScore(std::string &key, std::string &member, double *score) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZScore(key, member, score);
  } else {
    return Status::NotFound("key not exist");
  }
}

// used
Status PCache::ZRangebylex(std::string &key, std::string &min, std::string &max, std::vector<std::string> *members) {
  int cache_index = CacheIndex(key);
  std::lock_guard lm(*cache_mutexs_[cache_index]);
  auto cache_obj = caches_[cache_index];

  if (cache_obj->Exists(key)) {
    return cache_obj->ZRangebylex(key, min, max, members);
  } else {
    return Status::NotFound("key not exist");
  }
}

// Status PCache::ZLexcount(std::string& key, std::string &min, std::string &max, uint64_t *len,
//                             const std::shared_ptr<DB>& db) {
//   if (CacheSizeEqsDB(key, db)) {
//     int cache_index = CacheIndex(key);
//     std::lock_guard lm(*cache_mutexs_[cache_index]);

//     return caches_[cache_index]->ZLexcount(key, min, max, len);
//   } else {
//     return Status::NotFound("key not in cache");
//   }
// }

// Status PCache::ZRemrangebylex(std::string& key, std::string &min, std::string &max,
//                                  const std::shared_ptr<DB>& db) {
//   if (CacheSizeEqsDB(key, db)) {
//     int cache_index = CacheIndex(key);
//     std::lock_guard lm(*cache_mutexs_[cache_index]);

//     return caches_[cache_index]->ZRemrangebylex(key, min, max);
//   } else {
//     return Status::NotFound("key not in cache");
//   }
// }

// /*-----------------------------------------------------------------------------
//  * Bit Commands
//  *----------------------------------------------------------------------------*/
// Status PCache::SetBit(std::string& key, size_t offset, int64_t value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->SetBit(key, offset, value);
// }

// Status PCache::SetBitIfKeyExist(std::string& key, size_t offset, int64_t value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   if (caches_[cache_index]->Exists(key)) {
//     return caches_[cache_index]->SetBit(key, offset, value);
//   }
//   return Status::NotFound("key not exist");
// }

// Status PCache::GetBit(std::string& key, size_t offset, int64_t *value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->GetBit(key, offset, value);
// }

// Status PCache::BitCount(std::string& key, int64_t start, int64_t end, int64_t *value, bool have_offset) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->BitCount(key, start, end, value, have_offset);
// }

// Status PCache::BitPos(std::string& key, int64_t bit, int64_t *value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->BitPos(key, bit, value);
// }

// Status PCache::BitPos(std::string& key, int64_t bit, int64_t start, int64_t *value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->BitPos(key, bit, start, value);
// }

// Status PCache::BitPos(std::string& key, int64_t bit, int64_t start, int64_t end, int64_t *value) {
//   int cache_index = CacheIndex(key);
//   std::lock_guard lm(*cache_mutexs_[cache_index]);
//   return caches_[cache_index]->BitPos(key, bit, start, end, value);
// }

Status PCache::InitWithoutLock(uint32_t cache_num, cache::CacheConfig *cache_cfg) {
  cache_status_ = PCACHE_STATUS_INIT;

  cache_num_ = cache_num;
  if (cache_cfg != nullptr) {
    cache::RedisCache::SetConfig(cache_cfg);
  }

  for (uint32_t i = 0; i < cache_num; ++i) {
    auto *cache = new cache::RedisCache();
    rocksdb::Status s = cache->Open();
    if (!s.ok()) {
      ERROR("PCache::InitWithoutLock Open cache failed");
      DestroyWithoutLock();
      cache_status_ = PCACHE_STATUS_NONE;
      return Status::Corruption("create redis cache failed");
    }
    caches_.push_back(cache);
    cache_mutexs_.push_back(std::make_shared<pstd::Mutex>());
  }
  cache_status_ = PCACHE_STATUS_OK;
  return Status::OK();
}

void PCache::DestroyWithoutLock(void) {
  cache_status_ = PCACHE_STATUS_DESTROY;

  for (auto iter = caches_.begin(); iter != caches_.end(); ++iter) {
    delete *iter;
  }
  caches_.clear();
  cache_mutexs_.clear();
}

int PCache::CacheIndex(const std::string &key) {
  auto crc = crc32(0L, (const Bytef *)key.data(), (int)key.size());
  return (int)(crc % caches_.size());
}

Status PCache::WriteKVToCache(std::string &key, std::string &value, int64_t ttl) {
  if (0 >= ttl) {
    if (PCache_TTL_NONE == ttl) {
      return SetnxWithoutTTL(key, value);
    } else {
      return Del({key});
    }
  } else {
    return Setnx(key, value, ttl);
  }
  return Status::OK();
}

Status PCache::WriteHashToCache(std::string &key, std::vector<storage::FieldValue> &fvs, int64_t ttl) {
  if (0 >= ttl) {
    if (PCache_TTL_NONE == ttl) {
      return HMSetnxWithoutTTL(key, fvs);
    } else {
      return Del({key});
    }
  } else {
    return HMSetnx(key, fvs, ttl);
  }
  return Status::OK();
}

Status PCache::WriteListToCache(std::string &key, std::vector<std::string> &values, int64_t ttl) {
  if (0 >= ttl) {
    if (PCache_TTL_NONE == ttl) {
      return RPushnxWithoutTTL(key, values);
    } else {
      return Del({key});
    }
  } else {
    return RPushnx(key, values, ttl);
  }
  return Status::OK();
}

Status PCache::WriteSetToCache(std::string &key, std::vector<std::string> &members, int64_t ttl) {
  if (0 >= ttl) {
    if (PCache_TTL_NONE == ttl) {
      return SAddnxWithoutTTL(key, members);
    } else {
      return Del({key});
    }
  } else {
    return SAddnx(key, members, ttl);
  }
  return Status::OK();
}

Status PCache::WriteZSetToCache(std::string &key, std::vector<storage::ScoreMember> &score_members, int64_t ttl) {
  if (0 >= ttl) {
    if (PCache_TTL_NONE == ttl) {
      return ZAddnxWithoutTTL(key, score_members);
    } else {
      return Del({key});
    }
  } else {
    return ZAddnx(key, score_members, ttl);
  }
  return Status::OK();
}

void PCache::PushKeyToAsyncLoadQueue(const char key_type, std::string &key, PClient *client) {
  cache_load_thread_->Push(key_type, key, client);
}

void PCache::ClearHitRatio(void) {
  std::unique_lock l(rwlock_);
  cache::RedisCache::ResetHitAndMissNum();
}
}  // namespace pikiwidb
