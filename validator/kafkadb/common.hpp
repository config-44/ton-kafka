#pragma once

#include "common/refcnt.hpp"
#include "errorcode.h"
#include "td/actor/PromiseFuture.h"
#include "td/utils/Status.h"
#include "td/utils/logging.h"
#include "vm/cells/Cell.h"
#include <chrono>

#define ERR(msg) td::Status::Error(ErrorCode::error, DBGSTR() + " " + msg)

namespace td {

td::string u64str(td::uint64 value);
td::string u64str(const td::string& hex_value);
td::string u1024str(const td::string& hex_value);

} // namespace td

namespace ton {
namespace kafkadb {
namespace util {

template <class T>
class PromiseKeeper {
public:
  td::Promise<T> p;
  PromiseKeeper(td::Promise<T> promise) : p(std::move(promise)) {}

  static std::shared_ptr<PromiseKeeper<T>> from(td::Promise<T>&& value) {
    return std::make_shared<PromiseKeeper<td::Unit>>(std::move(value));
  }
};

td::Result<td::Unit> dump_boc_file(td::Ref<vm::Cell> cell, std::string path);
std::chrono::milliseconds get_now_ms();

} // namespace util
} // namespace kafkadb
} // namespace ton
