#pragma once

#include "block-auto.h"
#include "block-parse.h"
#include "common/refint.h"
#include "td/utils/Status.h"
#include "td/utils/optional.h"
#include "vm/cells/CellSlice.h"

namespace ton {
namespace kafkadb {
namespace parser {

struct StorageUsed {
  td::RefInt256 cells; // cells:(VarUInteger 7)
  td::RefInt256 bits; // bits:(VarUInteger 7)
  td::RefInt256 public_cells; // public_cells:(VarUInteger 7)

  explicit StorageUsed(
    td::RefInt256 cells,
    td::RefInt256 bits,
    td::RefInt256 public_cells
  ) : cells(std::move(cells))
    , bits(std::move(bits))
    , public_cells(std::move(public_cells)) {}

  static td::Result<StorageUsed> unpack(td::Ref<vm::CellSlice> cs);
};

struct StorageInfo {
  StorageUsed used; // used:StorageUsed
  td::uint32 last_paid; // last_paid:uint32
  td::optional<td::RefInt256> due_payment; // due_payment:(Maybe Grams)

  explicit StorageInfo(
    StorageUsed used, 
    td::uint32 last_paid, 
    td::optional<td::RefInt256> due_payment
  ) : used(std::move(used))
    , last_paid(last_paid)
    , due_payment(std::move(due_payment)) {}

  static td::Result<StorageInfo> unpack(td::Ref<vm::CellSlice> cs);
};

struct StateInit {
  struct TickTock {
    bool tick; // tick:Bool
    bool tock; // tock:Bool
  };

  td::optional<int> split_depth; // split_depth:(Maybe (## 5))
  td::optional<TickTock> special; // special:(Maybe TickTock)
  td::optional<td::Ref<vm::Cell>> code; // code:(Maybe ^Cell)
  td::optional<td::Ref<vm::Cell>> data; // data:(Maybe ^Cell)
  td::optional<td::Ref<vm::Cell>> library; // library:(Maybe ^Cell)

  explicit StateInit(
    td::optional<int> split_depth,
    td::optional<TickTock> special,
    td::optional<td::Ref<vm::Cell>> code,
    td::optional<td::Ref<vm::Cell>> data,
    td::optional<td::Ref<vm::Cell>> library
  ) : split_depth(split_depth)
    , special(special)
    , code(std::move(code))
    , data(std::move(data))
    , library(std::move(library)) {}

  static td::Result<StateInit> unpack(td::Ref<vm::CellSlice> cs);
};

struct AccountState$00 { /* account_uninit$00 */ };

struct AccountState$1 {
  StateInit inner; // _:StateInit

  explicit AccountState$1(StateInit inner) : inner(std::move(inner)) {}
  static td::Result<AccountState$1> unpack(td::Ref<vm::CellSlice> cs);
};

struct AccountState$01 {
  td::Bits256 state_hash; // state_hash:bits256

  explicit AccountState$01(td::Bits256 state_hash) : state_hash(state_hash) {}
  static td::Result<AccountState$01> unpack(td::Ref<vm::CellSlice> cs);
};

using AccountState_variant_t = absl::variant<
  AccountState$00, // account_uninit$00
  AccountState$1, // account_active$1
  AccountState$01 // account_frozen$01
>;

struct AccountState {
  AccountState_variant_t inner;

  explicit AccountState(AccountState_variant_t inner) : inner(std::move(inner)) {}
  static td::Result<AccountState> unpack(td::Ref<vm::CellSlice> cs);
};

struct AccountStorage {
  td::uint64 last_trans_lt; // last_trans_lt:uint64
  td::RefInt256 balance; // balance:CurrencyCollection
  AccountState state; // state:AccountState 

  explicit AccountStorage(
    td::uint64 last_trans_lt,
    td::RefInt256 balance,
    AccountState state
  ) : last_trans_lt(last_trans_lt)
    , balance(std::move(balance))
    , state(std::move(state)) {}

  static td::Result<AccountStorage> unpack(td::Ref<vm::CellSlice> cs);
};

struct Account$1 {
  block::gen::MsgAddressInt::Record_addr_std addr; // addr:MsgAddressInt
  StorageInfo storage_stat; // storage_stat:StorageInfo
  AccountStorage storage; // storage:AccountStorage

  explicit Account$1(
    block::gen::MsgAddressInt::Record_addr_std addr,
    StorageInfo storage_stat,
    AccountStorage storage
  ) : addr(std::move(addr))
    , storage_stat(std::move(storage_stat))
    , storage(std::move(storage)) {}

  static td::Result<Account$1> unpack(td::Ref<vm::CellSlice> cs);
};

struct Account {
  td::optional<Account$1> inner;

  explicit Account(td::optional<Account$1> inner) : inner(std::move(inner)) {}
  static td::Result<Account> unpack(td::Ref<vm::CellSlice> cs);
};

}  // namespace parser
}  // namespace kafkadb
}  // namespace ton
