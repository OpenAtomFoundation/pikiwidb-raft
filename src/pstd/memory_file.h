/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <string>

namespace pstd {

class InputMemoryFile {
 public:
  InputMemoryFile();
  ~InputMemoryFile();

  bool Open(const char* file);
  void Close();

  const char* Read(std::size_t& len);
  template <typename T>
  T Read();
  void Skip(std::size_t len);

  bool IsOpen() const;

 private:
  bool MapReadOnly();

  int file_ = -1;
  char* pMemory_ = nullptr;
  std::size_t offset_ = 0;
  std::size_t size_ = 0;
};

template <typename T>
inline T InputMemoryFile::Read() {
  T res(*reinterpret_cast<T*>(pMemory_ + offset_));
  offset_ += sizeof(T);

  return res;
}

class OutputMemoryFile {
 public:
  OutputMemoryFile();
  ~OutputMemoryFile();

  bool Open(const std::string& file, bool bAppend = true);
  bool Open(const char* file, bool bAppend = true);
  void Close();
  bool Sync();

  void Truncate(std::size_t size);
  //!! if process terminated abnormally, erase the trash data
  void TruncateTailZero();

  void Write(const void* data, std::size_t len);
  template <typename T>
  void Write(const T& t);

  std::size_t Offset() const { return offset_; }
  bool IsOpen() const;

 private:
  bool MapWriteOnly();
  void ExtendFileSize(std::size_t size);
  void AssureSpace(std::size_t size);

  int file_ = -1;
  char* pMemory_ = nullptr;
  std::size_t offset_ = 0;
  std::size_t size_ = 0;
  std::size_t syncPos_ = 0;
};

template <typename T>
inline void OutputMemoryFile::Write(const T& t) {
  this->Write(&t, sizeof t);
}

}  // namespace pstd
