/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <filesystem>

#include "braft/file_system_adaptor.h"
#include "braft/macros.h"
#include "braft/snapshot.h"

#define PRAFT_SNAPSHOT_META_FILE "__raft_snapshot_meta"
#define PRAFT_SNAPSHOT_PATH "snapshot/snapshot_"
#define IS_RDONLY 0x01

namespace pikiwidb {

class PPosixFileSystemAdaptor : public braft::PosixFileSystemAdaptor {
 public:
  PPosixFileSystemAdaptor() {}
  ~PPosixFileSystemAdaptor() {}

  braft::FileAdaptor* open(const std::string& path, int oflag, const ::google::protobuf::Message* file_meta,
                           butil::File::Error* e) override;
  void AddAllFiles(const std::filesystem::path& dir, braft::LocalSnapshotMetaTable* snapshot_meta_memtable,
                   const std::string& path);

 private:
  braft::raft_mutex_t mutex_;
};

}  // namespace pikiwidb
