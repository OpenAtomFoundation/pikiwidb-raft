/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  psnapshot.cc

#include <filesystem>

#include "braft/file_system_adaptor.h"
#include "braft/local_file_meta.pb.h"
#include "braft/snapshot.h"
#include "config.h"
#include "psnapshot.h"
#include "store.h"

namespace pikiwidb {

struct PConfig;
extern PConfig g_config;

braft::FileAdaptor* PPosixFileSystemAdaptor::open(const std::string& path, int oflag,
                                                  const ::google::protobuf::Message* file_meta, butil::File::Error* e) {
  if ((oflag & 0x01) == 0) {  // This is a read operation
    bool found_other_files = false;
    auto found_pos = path.find("snapshot/snapshot_");
    std::string snapshot_path;

    // parse snapshot path
    if (found_pos != std::string::npos) {
      std::size_t after_found_pos = found_pos + std::string("snapshot/snaphot_").length();
      auto slash_pos = path.find('/', after_found_pos);
      if (slash_pos != std::string::npos) {
        snapshot_path = path.substr(0, slash_pos);
      }
    }

    // check whether snapshots have been created
    std::lock_guard guard(mutex_);
    if (!snapshot_path.empty()) {
      for (const auto& entry : std::filesystem::directory_iterator(snapshot_path)) {
        std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() || entry.is_directory()) {
          if (filename != "." && filename != ".." && filename.find("raft_snapshot_meta") == std::string::npos) {
            // If the path directory contains files other than raft_snapshot_meta, snapshots have been generated
            found_other_files = true;
            break;
          }
        }
      }
    }

    // Snapshot generation
    if (!found_other_files) {
      INFO("start generate snapshot");
      braft::LocalSnapshotMetaTable snapshot_meta_memtable;
      std::string meta_path = snapshot_path + "/" PBRAFT_SNAPSHOT_META_FILE;
      braft::FileSystemAdaptor* fs = braft::default_file_system();
      snapshot_meta_memtable.load_from_file(fs, meta_path);

      TasksVector tasks;
      tasks.reserve(g_config.databases);
      for (auto i = 0; i < g_config.databases; ++i) {
        tasks.push_back({TaskType::kCheckpoint, i, {{TaskArg::kCheckpointPath, snapshot_path}}});
      }

      PSTORE.DoSomeThingSpecificDB(tasks);
      PSTORE.WaitForCheckpointDone();
      add_all_files(snapshot_path, &snapshot_meta_memtable, snapshot_path);
      const int rc = snapshot_meta_memtable.save_to_file(fs, snapshot_path + "/" PBRAFT_SNAPSHOT_META_FILE);
      if (rc == 0) {
        INFO("Succeed to save, path: {}", snapshot_path);
      } else {
        ERROR("Fail to save, path: {}", snapshot_path);
      }
      INFO("end generate snapshot");
    }
  }

  return braft::PosixFileSystemAdaptor::open(path, oflag, file_meta, e);
}

void PPosixFileSystemAdaptor::add_all_files(const std::filesystem::path& dir,
                                            braft::LocalSnapshotMetaTable* snapshot_meta_memtable,
                                            const std::string& path) {
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_directory()) {
      if (entry.path() != "." && entry.path() != "..") {
        INFO("dir_path = {}", entry.path().string());
        add_all_files(entry.path(), snapshot_meta_memtable, path);
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
