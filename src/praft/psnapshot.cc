/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  psnapshot.cc

#include "psnapshot.h"

#include "braft/local_file_meta.pb.h"
#include "butil/files/file_path.h"

#include "pstd/log.h"

#include "config.h"
#include "store.h"

namespace pikiwidb {

extern PConfig g_config;

braft::FileAdaptor* PPosixFileSystemAdaptor::open(const std::string& path, int oflag,
                                                  const ::google::protobuf::Message* file_meta, butil::File::Error* e) {
  if ((oflag & IS_RDONLY) == 0) {  // This is a read operation
    bool snapshots_exists = false;
    std::string snapshot_path;

    // parse snapshot path
    butil::FilePath parse_snapshot_path(path);
    std::vector<std::string> components;
    parse_snapshot_path.GetComponents(&components);
    for (auto component : components) {
      snapshot_path += component + "/";
      if (component.find("snapshot_") != std::string::npos) {
        break;
      }
    }
    // check whether snapshots have been created
    std::lock_guard<braft::raft_mutex_t> guard(mutex_);
    if (!snapshot_path.empty()) {
      for (const auto& entry : std::filesystem::directory_iterator(snapshot_path)) {
        std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() || entry.is_directory()) {
          if (filename != "." && filename != ".." && filename.find(PRAFT_SNAPSHOT_META_FILE) == std::string::npos) {
            // If the path directory contains files other than raft_snapshot_meta, snapshots have been generated
            snapshots_exists = true;
            break;
          }
        }
      }
    }

    // Snapshot generation
    if (!snapshots_exists) {
      braft::LocalSnapshotMetaTable snapshot_meta_memtable;
      std::string meta_path = snapshot_path + "/" PRAFT_SNAPSHOT_META_FILE;
      INFO("start to generate snapshot in path {}", snapshot_path);
      braft::FileSystemAdaptor* fs = braft::default_file_system();
      assert(fs);
      snapshot_meta_memtable.load_from_file(fs, meta_path);

      TasksVector tasks(1, {TaskType::kCheckpoint, 0, {{TaskArg::kCheckpointPath, snapshot_path}}, true});
      PSTORE.HandleTaskSpecificDB(tasks);
      AddAllFiles(snapshot_path, &snapshot_meta_memtable, snapshot_path);

      auto rc = snapshot_meta_memtable.save_to_file(fs, meta_path);
      if (rc == 0) {
        INFO("Succeed to save snapshot in path {}", snapshot_path);
      } else {
        ERROR("Fail to save snapshot in path {}", snapshot_path);
      }
      INFO("generate snapshot completed in path {}", snapshot_path);
    }
  }

  return braft::PosixFileSystemAdaptor::open(path, oflag, file_meta, e);
}

void PPosixFileSystemAdaptor::AddAllFiles(const std::filesystem::path& dir,
                                          braft::LocalSnapshotMetaTable* snapshot_meta_memtable,
                                          const std::string& path) {
  assert(snapshot_meta_memtable);
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_directory()) {
      if (entry.path() != "." && entry.path() != "..") {
        INFO("dir_path = {}", entry.path().string());
        AddAllFiles(entry.path(), snapshot_meta_memtable, path);
      }
    } else {
      INFO("file_path = {}", std::filesystem::relative(entry.path(), path).string());
      braft::LocalFileMeta meta;
      if (snapshot_meta_memtable->add_file(std::filesystem::relative(entry.path(), path), meta) != 0) {
        WARN("Failed to add file");
      }
    }
  }
}

}  // namespace pikiwidb
