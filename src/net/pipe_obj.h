#pragma once

#include "event_obj.h"

namespace pikiwidb::internal {

class PipeObject : public EventObject {
 public:
  PipeObject();
  ~PipeObject() override;

  PipeObject(const PipeObject&) = delete;
  void operator=(const PipeObject&) = delete;

  int Fd() const override;
  bool HandleReadEvent() override;
  bool HandleWriteEvent() override;
  void HandleErrorEvent() override;

  bool Notify();

 private:
  int read_fd_;
  int write_fd_;
};

}  // namespace pikiwidb::internal
