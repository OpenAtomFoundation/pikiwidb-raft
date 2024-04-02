/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <mutex>

#include "braft/file_system_adaptor.h"

#define PBRAFT_SNAPSHOT_META_FILE "__raft_snapshot_meta"

namespace braft {
class LocalSnapshotMetaTable;
}

namespace pikiwidb {

class PPosixFileSystemAdaptor : public braft::PosixFileSystemAdaptor {
 public:
  PPosixFileSystemAdaptor() {}
  ~PPosixFileSystemAdaptor() {}

  braft::FileAdaptor* open(const std::string& path, int oflag, const ::google::protobuf::Message* file_meta,
                           butil::File::Error* e) override;
  void add_all_files(const std::filesystem::path& dir, braft::LocalSnapshotMetaTable* snapshot_meta_memtable,
                     const std::string& path);

 private:
  std::mutex mutex_;
};

}  // namespace pikiwidb
