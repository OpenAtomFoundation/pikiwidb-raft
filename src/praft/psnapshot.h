/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "braft/file_system_adaptor.h"
#include "praft.h"

namespace pikiwidb {

class PPosixFileSystemAdaptor : public braft::PosixFileSystemAdaptor {
 public:
  PPosixFileSystemAdaptor() {}
  ~PPosixFileSystemAdaptor() {}

  braft::FileAdaptor* open(const std::string& path, int oflag, const ::google::protobuf::Message* file_meta,
                           butil::File::Error* e) override;
};

}  // namespace pikiwidb
