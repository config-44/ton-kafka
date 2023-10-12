#include "common.hpp"
#include "td/utils/Status.h"
#include "td/utils/check.h"
#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "vm/boc.h"
#include <chrono>
#include <iomanip>
#include <utility>

namespace td {

td::string u64str(td::uint64 value) {
  std::ostringstream hex_value_ss;
  hex_value_ss << std::hex << value;
  auto hex_value = hex_value_ss.str();

  auto spec_size = (hex_value.size() - 1);
  CHECK(spec_size < 15);
  
  std::ostringstream result_ss;
  result_ss << std::hex 
            << spec_size 
            << hex_value;

  return result_ss.str();
}

td::string u64str(const td::string& hex_value) {
  auto spec_size = (hex_value.size() - 1);
  CHECK(spec_size < 15);

  std::ostringstream result_ss;
  result_ss << std::hex 
            << spec_size 
            << hex_value;

  return result_ss.str();
}

td::string u1024str(const td::string& hex_value) {
  auto spec_size = (hex_value.size() - 1);
  CHECK(spec_size < 255);

  std::ostringstream result_ss;
  result_ss << std::hex 
            << std::setw(2) 
            << std::setfill('0') 
            << spec_size 
            << hex_value;

  return result_ss.str();
}

} // namespace td

namespace ton {
namespace kafkadb {
namespace util {

td::Result<td::Unit> dump_boc_file(td::Ref<vm::Cell> cell, std::string path) {
  vm::BagOfCells boc;
  boc.add_root(std::move(cell));

  auto res = boc.import_cells();
  if (res.is_error()) {
    return res;
  }

  auto data = boc.serialize_to_string(2);
  return td::write_file(path, data);
}

std::chrono::milliseconds get_now_ms() {
  using namespace std::chrono;
  auto ts = system_clock::now().time_since_epoch();
  return duration_cast<milliseconds>(ts);
}

} // namespace util
} // namespace kafkadb
} // namespace ton
