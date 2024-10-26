/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "router.h"
#include <cstring>
#include <memory>

Router::Router() {
  // maximum 100 parameters
  params.reserve(100);
}

Router *Router::add(const char *method, const char *pattern,
                    std::function<void(std::vector<std::string_view> &)> handler) {
  // step over any initial slash
  if (pattern[0] == '/') {
    pattern++;
  }

  std::vector<std::string> nodes;
  // nodes.push_back(method);

  const char *stop;
  const char *start = pattern;
  const char *end_ptr = pattern + strlen(pattern);
  do {
    stop = getNextSegment(start, end_ptr);

    // std::cout << "Segment(" << std::string(start, stop - start) << ")" << std::endl;

    nodes.emplace_back(start, stop - start);

    start = stop + 1;
  } while (stop != end_ptr);

  // if pattern starts with / then move 1+ and run inline slash parser

  add(nodes, handlers.size());
  handlers.push_back(handler);

  compile();
  return this;
}

void Router::compile() {
  compiled_tree.clear();
  compile_tree(tree);
}

void Router::route(const char *method, unsigned int method_length, const char *url, unsigned int url_length, userData) {
  handlers[lookup(url, url_length)](userData, params);
  params.clear();
}

void Router::add(const std::vector<std::string> &route, short handler) {
  Node *parent = tree;
  for (const std::string &node : route) {
    if (parent->children.find(node) == parent->children.end()) {
      parent->children[node] = std::shared_ptr<Node>(new Node({node, {}, handler}));
    }
    parent = parent->children[node];
  }
}

unsigned short Router::compile_tree(Node *n) {
  unsigned short nodeLength = 6 + n->name.length();
  for (const auto &c : n->children) {
    nodeLength += compile_tree(c.second);
  }

  unsigned short nodeNameLength = n->name.length();

  std::string compiledNode;
  compiledNode.append(reinterpret_cast<char *>(&nodeLength), sizeof(nodeLength));
  compiledNode.append(reinterpret_cast<char *>(&nodeNameLength), sizeof(nodeNameLength));
  compiledNode.append(reinterpret_cast<char *>(&n->handler), sizeof(n->handler));
  compiledNode.append(n->name.data(), n->name.length());

  compiled_tree = compiledNode + compiled_tree;
  return nodeLength;
}

const char *Router::find_node(const char *parent_node, const char *name, int name_length) {
  unsigned short nodeLength = *reinterpret_cast<const unsigned short *>(&parent_node[0]);
  unsigned short nodeNameLength = *reinterpret_cast<const unsigned short *>(&parent_node[2]);

  // std::cout << "Finding node: <" << std::string(name, name_length) << ">" << std::endl;

  const char *stoppp = parent_node + nodeLength;
  for (const char *candidate = parent_node + 6 + nodeNameLength; candidate < stoppp;) {
    unsigned short nodeLength = *reinterpret_cast<const unsigned short *>(&candidate[0]);
    unsigned short nodeNameLength = *reinterpret_cast<const unsigned short *>(&candidate[2]);

    // whildcard, parameter, equal
    if (nodeNameLength == 0) {
      return candidate;
    } else if (candidate[6] == ':') {
      // parameter

      // todo: push this pointer on the stack of args!
      params.push_back(std::string_view({name, static_cast<size_t>(name_length)}));

      return candidate;
    } else if (nodeNameLength == name_length && !memcmp(candidate + 6, name, name_length)) {
      return candidate;
    }

    candidate = candidate + nodeLength;
  }

  return nullptr;
}

const char *Router::getNextSegment(const char *start, const char *end) {
  const char *stop = static_cast<const char *>(memchr(start, '/', end - start));
  return stop ? stop : end;
}

int Router::lookup(const char *url, int length) {
  // all urls start with /
  url++;
  length--;

  const char *treeStart = static_cast<char *>(compiled_tree.data());

  const char *stop;
  const char *start = url;
  const char *end_ptr = url + length;
  do {
    stop = getNextSegment(start, end_ptr);

    // std::cout << "Matching(" << std::string(start, stop - start) << ")" << std::endl;

    if (nullptr == (treeStart = find_node(treeStart, start, stop - start))) {
      return -1;
    }

    start = stop + 1;
  } while (stop != end_ptr);

  return *reinterpret_cast<const short *>(&treeStart[4]);
}
