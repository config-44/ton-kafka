#include "parser.hpp"
#include "common.hpp"

namespace ton {
namespace kafkadb {
namespace parser {

using block::gen::t_VarUInteger_7;
using block::gen::t_VarUInteger_16;

td::Result<StorageUsed> StorageUsed::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::StorageUsed::Record storage_used;
  if (!tlb::csr_unpack(std::move(cs), storage_used)) {
    return ERR("StorageUsed: can not unpack");
  }

  block::gen::VarUInteger::Record v_cells;
  auto ok = tlb::csr_type_unpack(
    std::move(storage_used.cells),
    t_VarUInteger_7,
    v_cells
  );

  if (!ok) {
    return ERR("StorageUsed: can not unpack cells");
  }

  block::gen::VarUInteger::Record v_bits;
  ok = tlb::csr_type_unpack(
    std::move(storage_used.bits),
    t_VarUInteger_7,
    v_bits
  );

  if (!ok) {
    return ERR("StorageUsed: can not unpack bits");
  }

  block::gen::VarUInteger::Record v_public_cells;
  ok = tlb::csr_type_unpack(
    std::move(storage_used.public_cells),
    t_VarUInteger_7,
    v_public_cells
  );

  if (!ok) {
    return ERR("StorageUsed: can not unpack public_cells");
  }

  return StorageUsed{
    std::move(v_cells.value),
    std::move(v_bits.value),
    std::move(v_public_cells.value)
  };
}

td::Result<StorageInfo> StorageInfo::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::StorageInfo::Record info;
  if (!tlb::csr_unpack(std::move(cs), info)) {
    return ERR("StorageInfo: can not unpack");
  }

  auto used_r = StorageUsed::unpack(std::move(info.used));
  if (used_r.is_error()) {
    return used_r.move_as_error();
  }

  auto due_payment = td::optional<td::RefInt256>{};
  if (info.due_payment->prefetch_ulong(1) == 1) {
    vm::CellSlice& cs = info.due_payment.write();
    cs.advance(1);
    due_payment = block::tlb::t_Grams.as_integer_skip(cs);
  }

  return StorageInfo{
    used_r.move_as_ok(),
    info.last_paid,
    std::move(due_payment)
  };
}

td::Result<StateInit> StateInit::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::StateInit::Record state_init;
  if (!tlb::csr_unpack(std::move(cs), state_init)) {
    return ERR("StateInit: can not unpack");
  }

  td::optional<int> opt_split_depth{};
  if (state_init.split_depth->size() == 6) {
    opt_split_depth = static_cast<int>(
      state_init.split_depth->prefetch_ulong(6) - 32
    );
  }

  td::optional<TickTock> opt_special{};
  if (state_init.special->size() > 1) {
    int t = static_cast<int>(state_init.special->prefetch_ulong(3));
    if (t < 0) {
      return ERR("StateInit: invalid tick tock");
    }

    bool tick = t & 2;
    bool tock = t & 1;

    opt_special = TickTock{tick, tock};
  }

  td::optional<td::Ref<vm::Cell>> opt_code{};
  auto opt_code_v = state_init.code->prefetch_ref();
  if (opt_code_v.not_null()) {
    opt_code = std::move(opt_code_v);
  }

  td::optional<td::Ref<vm::Cell>> opt_data{};
  auto opt_data_v = state_init.data->prefetch_ref();
  if (opt_data_v.not_null()) {
    opt_data = std::move(opt_data_v);
  }

  td::optional<td::Ref<vm::Cell>> opt_library{};
  auto opt_library_v = state_init.library->prefetch_ref();
  if (opt_library_v.not_null()) {
    opt_library = std::move(opt_library_v);
  }

  return StateInit{
    opt_split_depth,
    opt_special,
    std::move(opt_code),
    std::move(opt_data),
    std::move(opt_library)
  };
}

td::Result<AccountState$1> AccountState$1::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::AccountState::Record_account_active account_state;
  if (!tlb::csr_unpack(std::move(cs), account_state)) {
    return ERR("StateInit: can not unpack");
  }

  auto r = StateInit::unpack(std::move(account_state.x));
  if (r.is_error()) {
    return r.move_as_error();
  }

  return AccountState$1{r.move_as_ok()};
}

td::Result<AccountState$01> AccountState$01::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::AccountState::Record_account_frozen account_state{};
  if (!tlb::csr_unpack(cs, account_state)) {
    return ERR("AccountState$01: can not unpack");
  }

  return AccountState$01{account_state.state_hash};
}

td::Result<AccountState> AccountState::unpack(td::Ref<vm::CellSlice> cs) {
  int tag = block::gen::t_AccountState.get_tag(*cs);

  if (tag == block::gen::AccountState::account_uninit) {
    return AccountState$00{};
  }

  if (tag == block::gen::AccountState::account_active) {
    auto res = AccountState$1::unpack(std::move(cs));
    if (res.is_error()) { return res.move_as_error(); }
    return res.move_as_ok(); // AccountState$1
  }

  if (tag == block::gen::AccountState::account_frozen) {
    auto res = AccountState$01::unpack(std::move(cs));
    if (res.is_error()) { return res.move_as_error(); }
    return res.move_as_ok(); // AccountState$01
  }

  return ERR("AccountState: unexpected tag");
}

td::Result<AccountStorage> AccountStorage::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::AccountStorage::Record account_storage;
  if (!tlb::csr_unpack(std::move(cs), account_storage)) {
    return ERR("AccountStorage: can not unpack");
  }
  
  block::gen::CurrencyCollection::Record account_cc;
  if (!tlb::csr_unpack(std::move(account_storage.balance), account_cc)) {
    return ERR("AccountStorage: can not unpack CurrencyCollection");
  }

  block::gen::VarUInteger::Record v_balance;
  auto ok = tlb::csr_type_unpack(
    std::move(account_cc.grams),
    t_VarUInteger_16,
    v_balance
  );

  if (!ok) {
    return ERR("StorageInfo: can not unpack grams");
  }

  auto state_r = AccountState::unpack(
    std::move(account_storage.state)
  );

  if (state_r.is_error()) { 
    return state_r.move_as_error(); 
  }

  return AccountStorage{
    account_storage.last_trans_lt,
    v_balance.value,
    state_r.move_as_ok()
  };
}

td::Result<Account$1> Account$1::unpack(td::Ref<vm::CellSlice> cs) {
  block::gen::Account::Record_account account;
  if (!tlb::csr_unpack(std::move(cs), account)) {
    return ERR("Account$1: can not unpack Record_account");
  }

  int tag = block::gen::t_MsgAddressInt.get_tag(*account.addr);
  if (tag != block::gen::MsgAddressInt::addr_std) {
    return ERR("Account$1: non std address not supported");
  }

  block::gen::MsgAddressInt::Record_addr_std address;
  if (!tlb::csr_unpack(std::move(account.addr), address)) {
    return ERR("Account$1: can not unpack account address");
  }

  auto storage_stat_r = StorageInfo::unpack(
    std::move(account.storage_stat)
  );

  if (storage_stat_r.is_error()) {
    return storage_stat_r.move_as_error();
  }

  auto storage_r = AccountStorage::unpack(
    std::move(account.storage)
  );

  if (storage_r.is_error()) {
    return storage_r.move_as_error();
  }

  return Account$1{
    std::move(address),
    storage_stat_r.move_as_ok(),
    storage_r.move_as_ok()
  };
}

td::Result<Account> Account::unpack(td::Ref<vm::CellSlice> cs) {
  int tag = static_cast<int>(cs->prefetch_ulong(1));

  if (tag == block::gen::Account::account) {
    auto r = Account$1::unpack(std::move(cs));
    if (r.is_error()) {
      return r.move_as_error();
    }

    return Account{r.move_as_ok()};
  } else if (tag == block::gen::Account::account_none) {
    return {};
  }

  return ERR("Account: unexpected tag");
}

}  // namespace parser
}  // namespace kafkadb
}  // namespace ton
