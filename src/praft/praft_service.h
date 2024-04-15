#pragma once

#include "praft.pb.h"

namespace pikiwidb {

class PRaft;

class DummyServiceImpl : public DummyService {
 public:
  explicit DummyServiceImpl(PRaft* praft) : praft_(praft) {}
  void DummyMethod(::google::protobuf::RpcController* controller, const ::pikiwidb::DummyRequest* request,
                   ::pikiwidb::DummyResponse* response, ::google::protobuf::Closure* done) override {}

 private:
  PRaft* praft_;
};

}  // namespace pikiwidb