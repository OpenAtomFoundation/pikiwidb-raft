/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

namespace net {

#ifdef __linux__
#  define HAVE_EPOLL 1
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  define HAVE_KQUEUE 1
#endif

#ifdef __linux__
#  define HAVE_ACCEPT4 1
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__ppc64__) || defined(__aarch64__) || defined(__64BIT__) || \
    defined(_LP64) || defined(__LP64__)
#  define HAVE_64BIT 1
#else
#  define HAVE_32BIT 1
#endif

}  // namespace net
