#include "octantpath/core/sha1.hpp"

#include <algorithm>
#include <cstring>

namespace octantpath::core {

namespace {

uint32_t rotate(uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

} // namespace

Sha1::Sha1() : state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0} {}

void Sha1::block(uint8_t const *data) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) w[i] = uint32_t(data[i * 4]) << 24 | uint32_t(data[i * 4 + 1]) << 16 | uint32_t(data[i * 4 + 2]) << 8 | data[i * 4 + 3];
  for (int i = 16; i < 80; ++i) w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  auto [a, b, c, d, e] = state_;
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
    else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
    else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
    else f = b ^ c ^ d, k = 0xCA62C1D6;
    auto t = rotate(a, 5) + f + e + k + w[i];
    e = d, d = c, c = rotate(b, 30), b = a, a = t;
  }
  state_[0] += a, state_[1] += b, state_[2] += c, state_[3] += d, state_[4] += e;
}

void Sha1::update(std::string_view data) {
  if (digest_) return;
  length_ += data.size();
  auto const *bytes = reinterpret_cast<uint8_t const *>(data.data());
  auto size = data.size();
  while (size > 0) {
    auto take = std::min(size, buffer_.size() - buffered_);
    std::memcpy(buffer_.data() + buffered_, bytes, take);
    buffered_ += take, bytes += take, size -= take;
    if (buffered_ == buffer_.size()) {
      block(buffer_.data());
      buffered_ = 0;
    }
  }
}

std::string Sha1::hex() {
  if (digest_) return *digest_;
  auto bits = length_ * 8;
  uint8_t pad = 0x80;
  update(std::string_view(reinterpret_cast<char const *>(&pad), 1));
  uint8_t zero = 0;
  while (buffered_ != 56) update(std::string_view(reinterpret_cast<char const *>(&zero), 1));
  uint8_t tail[8];
  for (int i = 0; i < 8; ++i) tail[i] = uint8_t(bits >> (56 - i * 8));
  update(std::string_view(reinterpret_cast<char const *>(tail), 8));
  static char const *const digits = "0123456789abcdef";
  std::string out;
  for (auto word : state_) {
    for (int i = 28; i >= 0; i -= 4) out += digits[(word >> i) & 15];
  }
  digest_ = out;
  return out;
}

} // namespace octantpath::core
