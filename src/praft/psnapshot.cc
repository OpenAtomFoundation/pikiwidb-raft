/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  psnapshot.cc

#include "psnapshot.h"
#include "config.h"
#include "pikiwidb.h"
#include "praft.h"
#include "store.h"

namespace pikiwidb {

struct PConfig;
extern PConfig g_config;

braft::FileAdaptor* PPosixFileSystemAdaptor::open(const std::string& path, int oflag,
                                                  const ::google::protobuf::Message* file_meta, butil::File::Error* e) {
  // checkpoint callback
  PRAFT.GenerateRealSnapshot();

  return braft::PosixFileSystemAdaptor::open(path, oflag, file_meta, e);
}

}  // namespace pikiwidb
