#include "common.hpp"
#include "producer.hpp"
#include "td/actor/ActorId.h"
#include "td/actor/PromiseFuture.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/check.h"
#include "td/utils/common.h"
#include "td/utils/int_types.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ton {
namespace kafkadb {
namespace producer {

KafkaConnection::KafkaConnection(KafkaConfig config) {
  std::stringstream broker_list_ss{};
  broker_list_ss << config.conn_cfg.addr.get_ip_str().str()
                  << ":"
                  << td::to_string(config.conn_cfg.addr.get_port());

  kafka::Properties props{};
  props.put("bootstrap.servers", broker_list_ss.str());
  props.put("message.timeout.ms", td::to_string(config.conn_cfg.timeout_ms));
  props.put("message.max.bytes", td::to_string(config.conn_cfg.max_bytes));

  producer_ = std::make_unique<KafkaProducer>(props);
}
  
void KafkaConnection::produce(ProducerRecord record, td::Promise<td::Unit> promise) {
  CHECK(producer_ != nullptr)

  try {
    producer_->syncSend(record);
  } catch (const std::exception& err) {
    promise.set_error(ERR("kafka message failed to be delivered: " + err.what()));
    return;
  } catch (...) {
    promise.set_error(ERR("kafka message failed to be delivered: unexpected error"));
    return;
  }

  promise.set_result(td::Unit());
}

td::actor::ActorOwn<KafkaTask> KafkaTask::create(
  KafkaConnection* conn,
  ProducerRecord task,
  td::Promise<td::Unit> promise
) {
  CHECK(conn != nullptr);
  return td::actor::create_actor<KafkaTask>(
    "kafkatask", 
    conn,
    std::move(task),
    std::move(promise)
  );
}

void KafkaTask::execute() {
  CHECK(conn_ != nullptr);
  conn_->produce(std::move(task_), std::move(promise_));
}

void KafkaMultiSender::finish_all() {
  if (!is_end()) {
    promise_.set_value(std::move(results_));
    end_ = true;
  }
}

void KafkaMultiSender::append_result(td::Result<td::Unit> res) {
  if (stop_on_error_ && res.is_error()) {
    end_ = true;
    end_with_error_ = true;
    promise_.set_error(res.move_as_error());
    return;
  }

  results_.push_back(std::move(res));
  counter_--;

  if (counter_ == 0 && is_join()) {
    finish_all();
  }
}

void KafkaMultiSender::start_up() {
  // if (timeout_.at() != 0) {
  //   alarm_timestamp() = timeout_;
  // }
}

bool KafkaMultiSender::is_end() {
  return end_;
}

bool KafkaMultiSender::is_join() {
  return join_;
}

bool KafkaMultiSender::end_with_error() {
  return end_with_error_;
}

void KafkaMultiSender::alarm() {
  promise_.set_error(ERR("there was an timeout alarm"));
  td::actor::actor_id(this).get_actor_unsafe().stop();
}

td::actor::ActorOwn<KafkaMultiSender> KafkaMultiSender::create(
  td::Timestamp timeout, 
  KafkaConnection* conn,
  bool stop_on_error,
  td::Promise<std::vector<td::Result<td::Unit>>> promise
) {
  return td::actor::create_actor<KafkaMultiSender>(
    "kafkamultisender", 
    timeout,
    conn,
    stop_on_error,
    std::move(promise)
  );
};

void KafkaMultiSender::process_task(ProducerRecord task) {
  CHECK(!is_end() && !is_join());
  counter_++;

  auto P = td::PromiseCreator::lambda([
    SelfId = td::actor::core::actor_id(this)
  ](td::Result<td::Unit> result) -> void {
    td::actor::send_closure(
      SelfId,
      &KafkaMultiSender::append_result,
      std::move(result)
    );
  });

  auto task_actor = KafkaTask::create(
    conn_,
    std::move(task),
    std::move(P)
  );

  td::actor::send_closure(task_actor, &KafkaTask::execute);
  task_actor.release();
}

void KafkaMultiSender::join_all() {
  CHECK(!is_join());
  join_ = true;
  
  if (counter_ == 0) {
    finish_all();
  }
}

td::actor::ActorOwn<KafkaManager> KafkaManager::create(KafkaConfig config) {
  return td::actor::create_actor<KafkaManager>(
    "kafkamanager",
    std::move(config)
  );
};

void KafkaManager::AccountVisitor::operator()(const parser::AccountState$00&) const {
  obj_->operator()("acc_type", td::JsonInt{CloudAccountType::uninit});
}

void KafkaManager::AccountVisitor::operator()(const parser::AccountState$1& state) const {
  obj_->operator()("acc_type", td::JsonInt{CloudAccountType::active});

  if (auto v = state.inner.special) {
    obj_->operator()("tick", td::JsonBool{(*v).tick});
    obj_->operator()("tock", td::JsonBool{(*v).tock});
  }

  if (auto v = state.inner.code) {
    obj_->operator()("code_hash", td::to_lower((*v)->get_hash().to_hex()));
  }
  
  if (auto v = state.inner.data) {
    obj_->operator()("data_hash", td::to_lower((*v)->get_hash().to_hex()));
  }

  if (auto v = state.inner.library) {
    obj_->operator()("library_hash", td::to_lower((*v)->get_hash().to_hex()));
  }
}

void KafkaManager::AccountVisitor::operator()(const parser::AccountState$01& state) const {
  obj_->operator()("acc_type", td::JsonInt{CloudAccountType::frozen});
  obj_->operator()("state_hash", td::to_lower(state.state_hash.to_hex()));
}

void KafkaManager::account_to_json(
  const parser::Account& account,
  td::JsonBuilder& jb,
  bool dec
) {
  auto obj = jb.enter_object();
  if (!account.inner) {
    obj.leave();
    return;
  }

  auto a = account.inner.value();

  // a.addr
  std::stringstream id_ss;
  id_ss << a.addr.workchain_id << ":" << a.addr.address.to_hex();
  obj("id", td::to_lower(id_ss.str()));
  obj("workchain_id", td::JsonInt{a.addr.workchain_id});

  // a.storage_stat
  obj("last_paid", td::JsonLong{a.storage_stat.last_paid});
  obj("bits", td::u64str(a.storage_stat.used.bits->to_hex_string()));
  obj("cells", td::u64str(a.storage_stat.used.cells->to_hex_string())); 
  obj("public_cells", td::u64str(a.storage_stat.used.public_cells->to_hex_string())); 

  if (dec) {
    obj("bits_dec", a.storage_stat.used.bits->to_dec_string());
    obj("cells_dec", a.storage_stat.used.cells->to_dec_string());
    obj("public_cells_dec", a.storage_stat.used.public_cells->to_dec_string());
  }

  // a.storage.
  obj("last_trans_lt", td::u64str(a.storage.last_trans_lt)); 
  obj("balance", td::u1024str(a.storage.balance->to_hex_string()));  

  if (dec) { 
    obj("last_trans_lt_dec", std::to_string(a.storage.last_trans_lt));
    obj("balance_dec", a.storage.balance->to_dec_string());
  }

  AccountVisitor visitor{&obj};
  absl::visit(visitor, a.storage.state.inner);
}

void KafkaManager::deleted_account_to_json(
  WorkchainId wc,
  StdSmcAddress addr,
  td::JsonBuilder& jb
) {
  auto obj = jb.enter_object();
  
  std::stringstream id_ss;
  id_ss << wc << ":" << addr.to_hex();

  obj("id", td::to_lower(id_ss.str()));
  obj("workchain_id", td::JsonInt{wc});
  obj("acc_type", td::JsonInt{CloudAccountType::non_exist});
}

kafka::clients::producer::ProducerRecord KafkaManager::fill_kafka_account_record(
  const std::string& addr_str_quoted,
  const std::string& account_json,
  std::vector<std::unique_ptr<char>>& kv_pointers
) {
  std::unique_ptr<char> kafka_k{new char[addr_str_quoted.size()]};
  std::memcpy(kafka_k.get(), addr_str_quoted.data(), addr_str_quoted.size());

  std::unique_ptr<char> kafka_v{new char[account_json.size()]};
  std::memcpy(kafka_v.get(), account_json.data(), account_json.size());

  kafka::clients::producer::ProducerRecord kafka_record{
    config_.topics_cfg.accounts_topic,
    kafka::Key{kafka_k.get(), addr_str_quoted.size()},
    kafka::Value{kafka_v.get(), account_json.size()}
  };

  kv_pointers.push_back(std::move(kafka_k));
  kv_pointers.push_back(std::move(kafka_v));

  return kafka_record;
}

kafka::clients::producer::ProducerRecord KafkaManager::fill_kafka_block_record(
  td::Ref<validator::BlockData> block_data,
  BlockSeqno masterchain_ref,
  std::vector<std::unique_ptr<char>>& kv_pointers
) {
  auto id = block_data->block_id();
  CHECK(id.root_hash.size() == 256);
  auto root_hash_size = id.root_hash.size() / 8;
  
  std::unique_ptr<char> kafka_k{new char[root_hash_size]};
  std::memcpy(kafka_k.get(), id.root_hash.data(), root_hash_size);
  
  std::unique_ptr<char> kafka_v{new char[block_data->data().size()]};
  std::memcpy(kafka_v.get(), block_data->data().data(), block_data->data().size());

  kafka::clients::producer::ProducerRecord kafka_record(
    config_.topics_cfg.blocks_topic,
    kafka::Key{kafka_k.get(), root_hash_size},
    kafka::Value{kafka_v.get(), block_data->data().size()}
  );

  kv_pointers.push_back(std::move(kafka_k));
  kv_pointers.push_back(std::move(kafka_v));

  std::time_t time_since = std::time(nullptr);
  std::asctime(std::localtime(&time_since));

  // msterchain seqno ref (4 BE bytes)
  // block file hash (32 BE bytes)
  // current (block event) unix timestamp (8 BE bytes)
  std::unique_ptr<char> headers{new char[4 + 32 + 8]};
  auto hptr = headers.get();
  int shift = 0;

  hptr[shift++] = static_cast<char>((masterchain_ref >> 24) & 0xFF); 
  hptr[shift++] = static_cast<char>((masterchain_ref >> 16) & 0xFF);
  hptr[shift++] = static_cast<char>((masterchain_ref >> 8) & 0xFF);
  hptr[shift++] = static_cast<char>(masterchain_ref & 0xFF);

  std::memcpy(hptr + shift, id.file_hash.data(), 32);
  shift += 32;

  hptr[shift++] = static_cast<char>((time_since >> 56) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 48) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 40) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 32) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 24) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 16) & 0xFF);
  hptr[shift++] = static_cast<char>((time_since >> 8) & 0xFF);
  hptr[shift++] = static_cast<char>(time_since & 0xFF);

  using hv = kafka::Header::Value;
  kafka_record.headers() = {{
      kafka::Header{"mc_seq_no", hv{hptr, 4}},
      kafka::Header{"file_hash", hv{hptr + 4, 32}},
      kafka::Header{"raw_block_timestamp", hv{hptr + 4 + 32, 8}},
  }};

  kv_pointers.push_back(std::move(headers));

  return kafka_record;
}

std::string KafkaManager::fmt_acc_quoted(int wc, td::BitArray<256> addr) {
  td::string addr_str_quoted;
  std::stringstream id_ss;
  id_ss << "\"" << wc << ":" << td::to_lower(addr.to_hex()) << "\"";
  return id_ss.str();
}

KafkaManager::FitsResult KafkaManager::is_record_fits_into_kafka(
  const ProducerRecord& record
) {
  td::int32 rec_size = INT32_MAX;
  if (record.value().size() < INT32_MAX) {
    rec_size = static_cast<td::int32>(record.value().size());
  }

  if (rec_size > config_.conn_cfg.max_bytes)  {
    return {false, rec_size};
  }

  return {true, rec_size};
}

void KafkaManager::emit_fits_warning(KafkaManager::FitsResult fits, td::string addr_str) {
  auto size_s = fits.size == INT32_MAX 
    ? "INT32_MAX(2147483647)" 
    : std::to_string(fits.size);

  LOG_KAFKA(WARNING) << "skiping account " << addr_str
                     << ", because the record size (" 
                     << size_s
                     << ") is larger than maximum ("
                     << config_.conn_cfg.max_bytes << ")";
}

void KafkaManager::produce_full_state(
  td::Ref<validator::ShardState> state, 
  td::Promise<td::Unit> promise
) {
  block::gen::ShardStateUnsplit::Record sstate;
  if (!(tlb::unpack_cell(state->root_cell(), sstate))) {
    promise.set_error(ERR("cannot unpack state"));
    return;
  }

  vm::AugmentedDictionary accounts_dict{
    vm::load_cell_slice_ref(sstate.accounts),
    256, 
    block::tlb::aug_ShardAccounts
  };

  auto kv_pointers = std::make_shared<std::vector<std::unique_ptr<char>>>();
  auto shared_p = util::PromiseKeeper<td::Unit>::from(std::move(promise));

  std::chrono::milliseconds start_ts_ms = util::get_now_ms();

  auto end_promise = td::PromiseCreator::lambda(
  [p = shared_p, kv = kv_pointers, start_ts_ms, blk_id = state->get_block_id().id]
  (td::Result<std::vector<td::Result<td::Unit>>> R) mutable -> void {
    CHECK(p != nullptr);

    if (R.is_error()) {
      auto r = R.move_as_error();
      p->p.set_error(ERR("top lvl error in promise: " + r.to_string()));
      return;
    }

    auto results = R.move_as_ok();
    for (auto& result : results) {
      if (result.is_error()) {
        auto r = result.move_as_error();
        p->p.set_error(ERR("inner lvl error in promise:" + r.to_string()));
        return;
      }
    }

    std::chrono::milliseconds end_ts_ms = util::get_now_ms();
    std::chrono::milliseconds diff_ts = end_ts_ms - start_ts_ms; 

    LOG_KAFKA(INFO) << "produced full state "
                    << blk_id.to_str_format()
                    << " with "
                    << results.size() 
                    << " elements, in "
                    << diff_ts.count() << " ms.";

    kv.reset(); // just to make sure 
    p->p.set_value(td::Unit());
  });

  auto sender = KafkaMultiSender::create(
    td::Timestamp::in(0),
    conn_.get(),
    true,
    std::move(end_promise)
  );

  auto sender_id = sender.release();

  accounts_dict.check_for_each_extra([=, kv = kv_pointers, promise = shared_p]
  (td::Ref<vm::CellSlice> v, td::Ref<vm::CellSlice> e, td::ConstBitPtr k, int n) mutable -> bool {
    CHECK(n == 256);

    if (sender_id.get_actor_unsafe().end_with_error()) { return false; }

    if (v.is_null()) {
      promise->p.set_error(ERR("null value"));
      return false;
    }

    td::Ref<vm::Cell> account_root = v->prefetch_ref();
    if (account_root.is_null()) {
      promise->p.set_error(ERR("null account_root"));
      return false;
    }

    td::Ref<vm::CellSlice> cs{true, vm::NoVmOrd(), account_root};
    if (!cs->is_valid()) {
      promise->p.set_error(ERR("can not cell -> ord cs"));
      return false;
    }

    auto account_r = parser::Account::unpack(std::move(cs));
    if (account_r.is_error()) {
      std::stringstream ssfn;
      ssfn << td::to_string(n) << ".boc";

      LOG_KAFKA(ERROR) << "dumping invalid account boc to as \"" << ssfn.str() << "\"";
      util::dump_boc_file(std::move(account_root), ssfn.str());

      auto r = account_r.move_as_error();
      promise->p.set_error(ERR("deserializing error: " + r.to_string()));
      return false;
    }

    auto account = account_r.move_as_ok();
    if (!account.inner) {
      LOG_KAFKA(WARNING) << "skiping account, can't do without address";
      return true;
    }

    auto jb = td::JsonBuilder{};
    account_to_json(account, jb, false);
    auto account_json = jb.string_builder().as_cslice().str();

    auto va = account.inner.value();
    auto addr_q = fmt_acc_quoted(va.addr.workchain_id, va.addr.address);
    auto record = fill_kafka_account_record(addr_q, account_json, *kv);

    auto fits = is_record_fits_into_kafka(record);
    if (!fits.is_fits)  {
      emit_fits_warning(fits, addr_q);
      return true;
    }

    td::actor::send_closure(
      sender_id, 
      &KafkaMultiSender::process_task, 
      std::move(record)
    );
    
    return true;
  });

  if (sender_id.get_actor_unsafe().end_with_error()) { return; }
  td::actor::send_closure(sender_id, &KafkaMultiSender::join_all);
}

void KafkaManager::produce_raw_block(
  BlockIdExt block_id,
  BlockSeqno masterchain_ref,
  td::Ref<validator::BlockData> block_data,
  td::Ref<validator::ShardState> state,
  td::Promise<td::Unit> promise
) {
  STDOUT_DBG() << "KafkaManager::produce_raw_block...";

  block::gen::ShardStateUnsplit::Record sstate;
  if (!(tlb::unpack_cell(state->root_cell(), sstate))) {
    promise.set_error(ERR("cannot unpack state"));
    return;
  }

  block::gen::Block::Record blk;
  block::gen::BlockExtra::Record extra;
  
  auto blk_ok = tlb::unpack_cell(block_data->root_cell(), blk);
  auto extra_ok = tlb::unpack_cell(blk.extra, extra);
  if (!blk_ok || !extra_ok) {
    promise.set_result(ERR("cannot unpack block"));
    return;
  }

  const static int KEY_SIZE = 256;

  auto account_blocks_dict = vm::AugmentedDictionary{
    vm::load_cell_slice_ref(extra.account_blocks), 
    KEY_SIZE,
    block::tlb::aug_ShardAccountBlocks
  };

  vm::AugmentedDictionary accounts_dict{
    vm::load_cell_slice_ref(sstate.accounts),
    KEY_SIZE,
    block::tlb::aug_ShardAccounts
  };

  auto kv_pointers = std::make_shared<std::vector<std::unique_ptr<char>>>();
  auto shared_p = util::PromiseKeeper<td::Unit>::from(std::move(promise));

  std::chrono::milliseconds start_ts_ms = util::get_now_ms();

  auto end_promise = td::PromiseCreator::lambda(
  [p = shared_p, kv = kv_pointers, start_ts_ms, block_id]
  (td::Result<std::vector<td::Result<td::Unit>>> R) mutable -> void {
    CHECK(p != nullptr);

    if (R.is_error()) {
      auto r = R.move_as_error();
      p->p.set_error(ERR("top lvl error in promise: " + r.to_string()));
      return;
    }

    auto results = R.move_as_ok();
    for (auto& result : results) {
      if (result.is_error()) {
        auto r = result.move_as_error();
        p->p.set_error(ERR("inner lvl error in promise:" + r.to_string()));
        return;
      }
    }

    std::chrono::milliseconds end_ts_ms = util::get_now_ms();
    std::chrono::milliseconds diff_ts = end_ts_ms - start_ts_ms; 

    LOG_KAFKA(INFO) << "produced raw block "
                    << block_id.id.to_str_format()
                    << " with "
                    << results.size() 
                    << " elements, in "
                    << diff_ts.count() << " ms.";

    kv.reset(); // just to make sure 
    p->p.set_value(td::Unit());
  });

  auto sender = KafkaMultiSender::create(
    td::Timestamp::in(0),
    conn_.get(),
    true,
    std::move(end_promise)
  );

  auto sender_id = sender.release();

  account_blocks_dict.check_for_each_extra(
  [=, &accounts_dict, kv = kv_pointers, promise = shared_p]
  (td::Ref<vm::CellSlice> v, td::Ref<vm::CellSlice> e, td::ConstBitPtr k, int n) mutable -> bool {
    CHECK(n == 256);

    if (sender_id.get_actor_unsafe().end_with_error()) { return false; }

    if (v.is_null()) {
      promise->p.set_error(ERR("null value"));
      return false;
    }

    StdSmcAddress address = k;
    auto acc_csr = accounts_dict.lookup(address);

    if (acc_csr.is_null()) { // deleted account handling
      auto jb = td::JsonBuilder{};
      deleted_account_to_json(block_id.id.workchain, address, jb);
      auto account_json = jb.string_builder().as_cslice().str();

      auto addr_q = fmt_acc_quoted(block_id.id.workchain, address);
      auto record = fill_kafka_account_record(
        addr_q,
        account_json,
        *kv
      );

      auto fits = is_record_fits_into_kafka(record);
      if (!fits.is_fits)  {
        emit_fits_warning(fits, addr_q);
        return true;
      }

      td::actor::send_closure(
        sender_id, 
        &KafkaMultiSender::process_task, 
        std::move(record)
      );

      return true;
    }

    td::Ref<vm::Cell> account_root = acc_csr->prefetch_ref();

    td::Ref<vm::CellSlice> cs{true, vm::NoVmOrd(), account_root};
    if (!cs->is_valid()) {
      promise->p.set_error(ERR("can not cell -> ord cs"));
      return false;
    }

    auto account_r = parser::Account::unpack(std::move(cs));
    if (account_r.is_error()) {
      std::stringstream ssfn;
      ssfn << td::to_string(n) << ".boc";

      LOG_KAFKA(ERROR) << "dumping invalid account boc to as \"" << ssfn.str() << "\"";
      util::dump_boc_file(std::move(account_root), ssfn.str());
      
      auto r = account_r.move_as_error();
      promise->p.set_error(ERR("deserializing error: " + r.to_string()));
      return false;
    }

    auto account = account_r.move_as_ok();

    auto jb = td::JsonBuilder{};
    account_to_json(account, jb, false);
    auto account_json = jb.string_builder().as_cslice().str();

    auto va = account.inner.value();
    auto addr_q = fmt_acc_quoted(va.addr.workchain_id, va.addr.address);
    auto record = fill_kafka_account_record(addr_q, account_json, *kv);

    STDOUT_DBG() << "changed account: " << addr_q;

    auto fits = is_record_fits_into_kafka(record);
    if (!fits.is_fits)  {
      emit_fits_warning(fits, addr_q);
      return true;
    }

    td::actor::send_closure(
      sender_id, 
      &KafkaMultiSender::process_task, 
      std::move(record)
    );

    return true;
  });

  if (sender_id.get_actor_unsafe().end_with_error()) { return; }

  auto blk_rec = fill_kafka_block_record(
    std::move(block_data),
    masterchain_ref,
    *kv_pointers
  );

  td::actor::send_closure(
    sender_id, 
    &KafkaMultiSender::process_task, 
    std::move(blk_rec)
  );

  td::actor::send_closure(sender_id, &KafkaMultiSender::join_all);
  STDOUT_DBG() << "...produce_raw_block";
}

} // namespace producer
} // namespace kafkadb
} // namespace ton
