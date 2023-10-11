#pragma once

#include "parser.hpp"
#include "common/refcnt.hpp"
#include "interfaces/shard.h"
#include "kafka/ProducerRecord.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/actor.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include <kafka/KafkaProducer.h>
#include <string>

namespace ton {
namespace kafkadb {
namespace producer {

using kafka::clients::producer::KafkaProducer;
using kafka::clients::producer::ProducerRecord;

struct KafkaTopicsConfig {
  td::string blocks_topic;
  td::string accounts_topic;
};

struct KafkaConnectionConfig {
  td::IPAddress addr;
  td::int32 timeout_ms{0};
  td::int32 max_bytes{0};
};

struct KafkaConfig {
  KafkaTopicsConfig topics_cfg;
  KafkaConnectionConfig conn_cfg;
};

class KafkaConnection {
private:
  std::unique_ptr<KafkaProducer> producer_;

public:
  KafkaConnection(KafkaConfig config);
  void produce(ProducerRecord record, td::Promise<td::Unit> promise);
};

class KafkaTask : public td::actor::Actor {
private:
  KafkaConnection* conn_;
  ProducerRecord task_;
  td::Promise<td::Unit> promise_;

public:
  KafkaTask(
    KafkaConnection* conn,
    ProducerRecord task,
    td::Promise<td::Unit> promise
  ) : conn_(conn)
    , task_(std::move(task))
    , promise_(std::move(promise)) {}

  static td::actor::ActorOwn<KafkaTask> create(
    KafkaConnection* conn,
    ProducerRecord task,
    td::Promise<td::Unit> promise
  );

  void execute();
};

class KafkaMultiSender : public td::actor::Actor {
private:
  bool join_{false};
  bool end_{false};
  bool end_with_error_{false};
  int counter_{0};
  std::vector<td::Result<td::Unit>> results_{};
  std::vector<ProducerRecord> tasks_{};

  td::Timestamp timeout_;
  KafkaConnection* conn_;
  bool stop_on_error_;
  td::Promise<std::vector<td::Result<td::Unit>>> promise_;

  void finish_all();
  
public:
  KafkaMultiSender(
    td::Timestamp timeout, 
    KafkaConnection* conn,
    bool stop_on_error,
    td::Promise<std::vector<td::Result<td::Unit>>> promise
  ) : timeout_(timeout)
    , conn_(conn)
    , stop_on_error_(stop_on_error)
    , promise_(std::move(promise)) {}

  bool is_end();
  bool is_join();
  bool end_with_error();
  
  void process_task(ProducerRecord task);
  void append_result(td::Result<td::Unit> res);
  void join_all();
  void start_up() override;
  void alarm() override;

  static td::actor::ActorOwn<KafkaMultiSender> create(
    td::Timestamp timeout, 
    KafkaConnection* conn,
    bool stop_on_error,
    td::Promise<std::vector<td::Result<td::Unit>>> promise
  );
};

class KafkaManager : public td::actor::Actor {
private:
  enum CloudAccountType {
    uninit = 0,
    active = 1,
    frozen = 2,
    non_exist = 3
  };

  struct AccountVisitor {
    td::JsonObjectScope* obj_;

    void operator()(const parser::AccountState$00&) const;
    void operator()(const parser::AccountState$1& state) const;
    void operator()(const parser::AccountState$01& state) const;
  };

  KafkaConfig config_;
  std::unique_ptr<KafkaConnection> conn_;

  static void account_to_json(
    const parser::Account& account,
    td::JsonBuilder& jb,
    bool dec
  );

  static void deleted_account_to_json(
    WorkchainId wc,
    StdSmcAddress addr,
    td::JsonBuilder& jb
  );

  struct FitsResult {
    bool is_fits;
    td::int32 size;
  };

  FitsResult is_record_fits_into_kafka(const ProducerRecord& record);
  void emit_fits_warning(FitsResult fits, td::string addr_str);

  static std::string fmt_acc_quoted(int workchain_id, td::BitArray<256> address);

  kafka::clients::producer::ProducerRecord fill_kafka_account_record(
    const std::string& addr_str_quoted,
    const std::string& account_json,
    std::vector<std::unique_ptr<char>>& kv_pointers
  );

  kafka::clients::producer::ProducerRecord fill_kafka_block_record(
    td::Ref<validator::BlockData> block_data,
    BlockSeqno masterchain_ref,
    std::vector<std::unique_ptr<char>>& kv_pointers
  );
public:
  KafkaManager(KafkaConfig config) : config_(std::move(config)) {
    conn_ = std::make_unique<KafkaConnection>(config);
  }

  static td::actor::ActorOwn<KafkaManager> create(KafkaConfig config);

  void produce_full_state(
    td::Ref<validator::ShardState> state, 
    td::Promise<td::Unit> promise
  );

  void produce_raw_block(
    BlockIdExt block_id,
    BlockSeqno masterchain_ref,
    td::Ref<validator::BlockData> block_data,
    td::Ref<validator::ShardState> state,
    td::Promise<td::Unit> promise
  );
};

} // namespace producer
} // namespace kafkadb
} // namespace ton
