#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace octantpath::core {

// SHA-1（マップの版の hash を確かめるため。OctantPath 法 2.1）。少しずつ足して、最後に 40 桁の小文字の 16 進で取り出す。
// hex() のあとは何度呼んでも同じ値を返す（update は足さない）
class Sha1 {
public:
  Sha1();
  void update(std::string_view data);
  std::string hex();

private:
  void block(uint8_t const *data);

  std::array<uint32_t, 5> state_;
  std::array<uint8_t, 64> buffer_{};
  size_t buffered_ = 0;
  uint64_t length_ = 0;
  std::optional<std::string> digest_;
};

} // namespace octantpath::core
