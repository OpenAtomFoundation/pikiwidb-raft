/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "kqueue_event.h"

#ifdef HAVE_KQUEUE
#  include "log.h"

namespace net {

const int BaseEvent::EVENT_READ = EVFILT_READ;
const int BaseEvent::EVENT_WRITE = EVFILT_WRITE;
const int BaseEvent::EVENT_ERROR = EV_ERROR;
const int BaseEvent::EVENT_HUB = EV_EOF;

bool KqueueEvent::Init() {
  evFd_ = kqueue();
  if (evFd_ == -1) {
    ERROR("kqueue error:{}", errno);
    return false;
  }
  if (mode_ & EVENT_MODE_READ) {
    AddEvent(0, listen_->Fd(), EVENT_READ);
  }
  if (pipe(pipeFd_) == -1) {
    ERROR("pipe error:{}", errno);
    return false;
  }

  AddEvent(0, pipeFd_[0], EVENT_READ);
  return true;
}

void KqueueEvent::AddEvent(uint64_t id, int fd, int mask) {
  struct kevent change;
#  ifdef HAVE_64BIT
  uint64_t udata = id;
#  else
  uint64_t *udata = new uint64_t;
  *udata = id;
#  endif
  EV_SET(&change, fd, mask, EV_ADD, 0, 0, reinterpret_cast<void *>(udata));
  if (kevent(EvFd(), &change, 1, nullptr, 0, nullptr) == -1) {
    ERROR("KqueueEvent AddEvent id:{},EvFd:{}，fd:{}, kevent error:{}", id, EvFd(), fd, errno);
  }
}

void KqueueEvent::DelEvent(int fd) {
  struct kevent change;
  EV_SET(&change, fd, EVENT_READ, EV_DELETE, 0, 0, nullptr);
  if (kevent(EvFd(), &change, 1, nullptr, 0, nullptr) == -1) {
    ERROR("KqueueEvent Del read Event EvFd:{}，fd:{}, kevent error:{}", EvFd(), fd, errno);
  }
  EV_SET(&change, fd, EVENT_WRITE, EV_DELETE, 0, 0, nullptr);
  if (kevent(EvFd(), &change, 1, nullptr, 0, nullptr) == -1) {
    ERROR("KqueueEvent Del write Event EvFd:{}，fd:{}, kevent error:{}", EvFd(), fd, errno);
  }
}

void KqueueEvent::AddWriteEvent(uint64_t id, int fd) { AddEvent(id, fd, EVENT_WRITE); }

void KqueueEvent::DelWriteEvent(uint64_t id, int fd) {
  struct kevent change;
  EV_SET(&change, fd, EVENT_WRITE, EV_DELETE, 0, 0, nullptr);
  if (kevent(EvFd(), &change, 1, nullptr, 0, nullptr) == -1) {
    ERROR("KqueueEvent Del write Event id:{},EvFd:{}，fd:{}, kevent error:{}", id, EvFd(), fd, errno);
  }
}

void KqueueEvent::EventPoll() {
  if (mode_ & EVENT_MODE_READ) {
    EventRead();
  } else {
    EventWrite();
  }
}

void KqueueEvent::EventRead() {
  struct kevent events[eventsSize];
  struct timespec *pTimeout = nullptr;
  struct timespec timeout {};
  if (timer_) {
    pTimeout = &timeout;
    int waitInterval = static_cast<int>(timer_->Interval());
    timeout.tv_sec = waitInterval / 1000;
    timeout.tv_nsec = (waitInterval % 1000) * 1000000;
  }

  while (running_.load()) {
    int nev = kevent(EvFd(), nullptr, 0, events, eventsSize, pTimeout);
    for (int i = 0; i < nev; ++i) {
      if ((events[i].flags & EVENT_HUB) || (events[i].flags & EVENT_ERROR)) {
        DoError(events[i], "");
        continue;
      }
      std::shared_ptr<Connection> conn;
      if (events[i].filter == EVENT_READ) {
        if (events[i].ident != listen_->Fd()) {
#  ifdef HAVE_64BIT
          auto connId = reinterpret_cast<uint64_t>(events[i].udata);
#  else
          auto _connId = reinterpret_cast<uint64_t *>(events[i].udata);
          uint64_t connId = *_connId;
#  endif
          conn = getConn_(connId);
        }
        DoRead(events[i], conn);
      } else if ((mode_ & EVENT_MODE_WRITE) && events[i].filter == EVENT_WRITE) {
#  ifdef HAVE_64BIT
        auto connId = reinterpret_cast<uint64_t>(events[i].udata);
#  else
        auto _connId = reinterpret_cast<uint64_t *>(events[i].udata);
        uint64_t connId = *_connId;
#  endif
        conn = getConn_(connId);
        if (!conn) {
          DoError(events[i], "write conn is null");
          continue;
        }
        DoWrite(events[i], conn);
      }
    }
    if (timer_) {
      timer_->OnTimer();
    }
  }
}

void KqueueEvent::EventWrite() {
  struct kevent events[eventsSize];
  while (running_.load()) {
    int nev = kevent(EvFd(), nullptr, 0, events, eventsSize, nullptr);
    for (int i = 0; i < nev; ++i) {
      if ((events[i].flags & EVENT_HUB) || (events[i].flags & EVENT_ERROR)) {
        DoError(events[i], "EventWrite error");
        continue;
      }
#  ifdef HAVE_64BIT
      auto connId = reinterpret_cast<uint64_t>(events[i].udata);
#  else
      auto _connId = reinterpret_cast<uint64_t *>(events[i].udata);
      uint64_t connId = *_connId;
#  endif
      auto conn = getConn_(connId);
      if (!conn) {
        DoError(events[i], "write conn is null");
        continue;
      }
      if (events[i].filter == EVENT_WRITE) {
        DoWrite(events[i], conn);
      }
    }
  }
}

void KqueueEvent::DoRead(const struct kevent &event, const std::shared_ptr<Connection> &conn) {
  if (event.ident == listen_->Fd()) {
    auto newConn = std::make_shared<Connection>(nullptr);
    auto connFd = listen_->OnReadable(newConn, nullptr);
    onCreate_(connFd, newConn);
  } else if (conn) {
    std::string readBuff;
    int ret = conn->netEvent_->OnReadable(conn, &readBuff);
    if (ret == NE_ERROR) {
      DoError(event, "read error,errno: " + std::to_string(errno));
      return;
    } else if (ret == NE_CLOSE) {
      DoError(event, "");
      return;
    }
#  ifdef HAVE_64BIT
    auto connId = reinterpret_cast<uint64_t>(event.udata);
#  else
    auto _connId = reinterpret_cast<uint64_t *>(event.udata);
    uint64_t connId = *_connId;
#  endif
    onMessage_(connId, std::move(readBuff));
  } else {
    DoError(event, "DoRead error");
  }
}

void KqueueEvent::DoWrite(const struct kevent &event, const std::shared_ptr<Connection> &conn) {
  auto ret = conn->netEvent_->OnWritable();
  if (ret == NE_ERROR) {
    DoError(event, "DoWrite error,errno: " + std::to_string(errno));
    return;
  }
  if (ret == 0) {
    DelWriteEvent(reinterpret_cast<uint64_t>(event.udata), conn->fd_);
#  ifdef HAVE_32BIT
    delete event.udata;
#  endif
  }
}

void KqueueEvent::DoError(const struct kevent &event, std::string &&err) {
  onClose_(reinterpret_cast<uint64_t>(event.udata), std::move(err));
#  ifdef HAVE_32BIT
  delete event.udata;
#  endif
}

}  // namespace net

#endif
