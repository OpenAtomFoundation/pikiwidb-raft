/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Router {
 public:
  Router();

  Router *add(const char *method, const char *pattern, std::function<void(std::vector<std::string_view> &)> handler);

  void compile();

  void route(const char *method, unsigned int method_length, const char *url, unsigned int url_length, userData);

 private:
  std::vector<std::function<void(std::vector<std::string_view> &)>> handlers;
  std::vector<std::string_view> params;

  struct Node {
    std::string name;
    std::map<std::string, std::shared_ptr<Node>> children;
    short handler;
  };

  std::shared_ptr<Node> tree = std::shared_ptr<Node>(new Node({"GET", {}, -1}));
  std::string compiled_tree;

  void add(const std::vector<std::string> &route, short handler);

  unsigned short compile_tree(Node *n);

  const char *find_node(const char *parent_node, const char *name, int name_length);

  // returns next slash from start or end
  const char *getNextSegment(const char *start, const char *end);

  // should take method also!
  int lookup(const char *url, int length);
};