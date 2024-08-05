/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <string>

#include "base_socket.h"
#include "callback_function.h"
#include "client_socket.h"
#include "io_thread.h"
#include "listen_socket.h"
#include "thread_manager.h"

namespace net {

template <typename T>
requires HasSetFdFunction<T>
class EventServer final {
 public:
  explicit EventServer(int8_t threadNum) : threadNum_(threadNum) { threadsManager_.reserve(threadNum_); }

  ~EventServer() = default;

  inline void SetOnInit(OnInit<T> &&func) { onInit_ = std::move(func); }

  inline void SetOnCreate(OnCreate<T> &&func) { onCreate_ = std::move(func); }

  inline void SetOnConnect(OnCreate<T> &&func) { onConnect_ = std::move(func); }

  inline void SetOnMessage(OnMessage<T> &&func) { onMessage_ = std::move(func); }

  inline void SetOnClose(OnClose<T> &&func) { onClose_ = std::move(func); }

  inline void AddListenAddr(const SocketAddr &addr) { listenAddrs_ = addr; }

  inline void SetRwSeparation(bool separation = true) { rwSeparation_ = separation; }

  void InitTimer(int64_t interval) { timer_ = std::make_shared<Timer>(interval); }

  inline int64_t AddTimerTask(const std::shared_ptr<ITimerTask> &task) { return timer_->AddTask(task); }

  inline void DelTimerTask(int64_t timerId) { timer_->DelTask(timerId); }

  std::pair<bool, std::string> StartServer(int64_t interval = 0);

  std::pair<bool, std::string> StartClientServer();

  // Stop the server
  void StopServer();

  // Send message to the client
  void SendPacket(const T &conn, std::string &&msg);

  // Server Active close the connection
  void CloseConnection(const T &conn);

  // When the service is started, the main thread is blocked,
  // and when all the subthreads are finished, the function unblocks and returns
  void Wait() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return !running_.load(); });
  }

  void TCPConnect(const SocketAddr &addr, OnCreate<T> onConnect, const std::function<void(std::string)> &cb);

  void TCPConnect(const SocketAddr &addr, const std::function<void(std::string)> &cb);

 private:
  int StartThreadManager(bool serverMode);

 private:
  OnInit<T> onInit_;  // The callback function used to initialize data before creating a connection

  OnCreate<T> onCreate_;  // The callback function when the connection is created

  OnCreate<T> onConnect_;  // The callback function when the client connection succeed

  OnMessage<T> onMessage_;  // The callback function when the message is received

  OnClose<T> onClose_;  // The callback function when the connection is closed

  SocketAddr listenAddrs_;  // The address to listen on

  std::atomic<bool> running_ = true;  // Whether the server is running

  bool rwSeparation_ = true;  // Whether to separate read and write

  int8_t threadNum_ = 1;  // The number of threads

  std::vector<std::unique_ptr<ThreadManager<T>>> threadsManager_;

  std::mutex mtx_;
  std::condition_variable cv_;

  std::shared_ptr<Timer> timer_;
};

template <typename T>
requires HasSetFdFunction<T> std::pair<bool, std::string> EventServer<T>::StartServer(int64_t interval) {
  if (threadNum_ <= 0) {
    return std::pair(false, "thread num must be greater than 0");
  }
  if (!onInit_) {
    return std::pair(false, "OnInit must be set");
  }
  if (!onCreate_) {
    return std::pair(false, "OnCreate must be set");
  }
  if (!onMessage_) {
    return std::pair(false, "OnMessage must be set");
  }
  if (!onClose_) {
    return std::pair(false, "OnClose must be set");
  }

  if (interval > 0 && !timer_) {
    InitTimer(interval);
  }

  for (int8_t i = 0; i < threadNum_; ++i) {
    auto tm = std::make_unique<ThreadManager<T>>(i, rwSeparation_);
    tm->SetOnInit(onInit_);
    tm->SetOnCreate(onCreate_);
    tm->SetOnConnect(onConnect_);
    tm->SetOnMessage(onMessage_);
    tm->SetOnClose(onClose_);
    threadsManager_.emplace_back(std::move(tm));
  }

  if (StartThreadManager(true) != static_cast<int>(NetListen::OK)) {
    return std::pair(false, "StartThreadManager function error");
  }

  return std::pair(true, "");
}

template <typename T>
requires HasSetFdFunction<T> std::pair<bool, std::string> EventServer<T>::StartClientServer() {
  if (threadNum_ <= 0) {
    return std::pair(false, "thread num must be greater than 0");
  }
  if (!onInit_) {
    return std::pair(false, "OnInit must be set");
  }
  if (!onConnect_) {
    return std::pair(false, "OnConnect must be set");
  }
  if (!onMessage_) {
    return std::pair(false, "OnMessage must be set");
  }
  if (!onClose_) {
    return std::pair(false, "OnClose must be set");
  }

  for (int8_t i = 0; i < threadNum_; ++i) {
    auto tm = std::make_unique<ThreadManager<T>>(i, rwSeparation_);
    tm->SetOnInit(onInit_);
    tm->SetOnConnect(onConnect_);
    tm->SetOnMessage(onMessage_);
    tm->SetOnClose(onClose_);
    threadsManager_.emplace_back(std::move(tm));
  }

  if (StartThreadManager(false) != static_cast<int>(NetListen::OK)) {
    return std::pair(false, "StartThreadManager function error");
  }

  return std::pair(true, "");
}

template <typename T>
requires HasSetFdFunction<T>
void EventServer<T>::StopServer() {
  bool expected = true;
  if (running_.compare_exchange_strong(expected, false)) {
    for (const auto &thread : threadsManager_) {
      thread->Stop();
    }
  }
  cv_.notify_one();
}

template <typename T>
requires HasSetFdFunction<T>
void EventServer<T>::SendPacket(const T &conn, std::string &&msg) {
  int thIndex;
  if constexpr (IsPointer_v<T>) {
    thIndex = conn->GetThreadIndex();
  } else {
    thIndex = conn.GetThreadIndex();
  }
  threadsManager_[thIndex]->SendPacket(conn, std::move(msg));
}

template <typename T>
requires HasSetFdFunction<T>
void EventServer<T>::CloseConnection(const T &conn) {
  int thIndex;
  int connId;
  if constexpr (IsPointer_v<T>) {
    thIndex = conn->GetThreadIndex();
    connId = conn->GetConnId();
  } else {
    thIndex = conn.GetThreadIndex();
    connId = conn.GetConnId();
  }

  threadsManager_[thIndex]->CloseConnection(connId);
}

template <typename T>
requires HasSetFdFunction<T>
void EventServer<T>::TCPConnect(const SocketAddr &addr, OnCreate<T> onConnect,
                                const std::function<void(std::string)> &cb) {
  auto clientSocket = std::make_unique<ClientSocket>(addr);
  clientSocket->SetFailCallback(cb);
  if (!clientSocket->Connect()) {
    return;
  }

  threadsManager_[0]->TCPConnect(addr, std::move(clientSocket), onConnect);
}

template <typename T>
requires HasSetFdFunction<T>
void EventServer<T>::TCPConnect(const SocketAddr &addr, const std::function<void(std::string)> &cb) {
  auto clientSocket = std::make_unique<ClientSocket>(addr);
  clientSocket->SetFailCallback(cb);
  if (!clientSocket->Connect()) {
    return;
  }

  threadsManager_[0]->TCPConnect(addr, std::move(clientSocket));
}

template <typename T>
requires HasSetFdFunction<T>
int EventServer<T>::StartThreadManager(bool serverMode) {
  std::shared_ptr<ListenSocket> listen(ListenSocket::CreateTCPListen());
  if (serverMode) {
    listen->SetListenAddr(listenAddrs_);

    if (auto ret = listen->Init() != static_cast<int>(NetListen::OK)) {
      return ret;
    }
  }

  int i = 0;
  for (const auto &thread : threadsManager_) {
    if (i > 0 && ListenSocket::REUSE_PORT && serverMode) {
      listen.reset(ListenSocket::CreateTCPListen());
      listen->SetListenAddr(listenAddrs_);
      if (auto ret = listen->Init() != static_cast<int>(NetListen::OK)) {
        return ret;
      }
    }

    // timer only works in the first thread
    bool ret = i == 0 ? thread->Start(listen, timer_) : thread->Start(listen, nullptr);
    if (!ret) {
      return -1;
    }
    ++i;
  }

  return static_cast<int>(NetListen::OK);
}

}  // namespace net
