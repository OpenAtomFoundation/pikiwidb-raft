// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  The declarations of a set of instructions and functions
  related to Raft are written here.
 */

#pragma once

#include <string_view>

#include "base_cmd.h"

namespace pikiwidb {
class PRaft;

/*
 * RAFT.NODE ADD [id] [address:port]
 *   Add a new node to the cluster.  The [id] can be an explicit non-zero value,
 *   or zero to let the cluster choose one.
 * Reply:
 *   -NOCLUSTER ||
 *   -LOADING ||
 *   -CLUSTERDOWN ||
 *   -MOVED <slot> <addr>:<port> ||
 *   *2
 *   :<new node id>
 *   :<group_id>
 *
 * RAFT.NODE REMOVE [id]
 *   Remove an existing node from the cluster.
 * Reply:
 *   -NOCLUSTER ||
 *   -LOADING ||
 *   -CLUSTERDOWN ||
 *   -MOVED <slot> <addr>:<port> ||
 *   +OK
 */
class RaftNodeCmd : public BaseCmd {
 public:
  RaftNodeCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoCmdAdd(PClient *client);
  void DoCmdRemove(PClient *client);
  void DoCmdSnapshot(PClient *client);

 private:
  PRaft *praft_ = nullptr;
  std::string group_id_;

  static constexpr std::string_view kAddCmd = "ADD";
  static constexpr std::string_view kRemoveCmd = "REMOVE";
  static constexpr std::string_view kDoSnapshot = "DOSNAPSHOT";
};

/*
 * RAFT.CLUSTER INIT [group_id]
 *   Initializes a new Raft cluster.
 * Reply:
 *   +OK <group_id>
 *
 * RAFT.CLUSTER JOIN <group_id> <addr:port>
 *   Join an existing cluster. The operation is asynchronous and may take place/retry in the background.
 * Reply:
 *   +OK
 */
class RaftClusterCmd : public BaseCmd {
 public:
  RaftClusterCmd(const std::string &name, int16_t arity);

 protected:
  bool DoInitial(PClient *client) override;

 private:
  void DoCmd(PClient *client) override;
  void DoCmdInit(PClient *client);
  void DoCmdJoin(PClient *client);

  void ClearPaftCtx();

 private:
  PRaft *praft_ = nullptr;

  static constexpr std::string_view kInitCmd = "INIT";
  static constexpr std::string_view kJoinCmd = "JOIN";
};

}  // namespace pikiwidb
