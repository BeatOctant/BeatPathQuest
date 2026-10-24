#include "octantpath/core/contract_version.hpp"

#include <charconv>

namespace octantpath::core {

std::optional<ContractVersion> ContractVersion::parse(std::string_view text) {
  auto dot = text.find('.');
  if (dot == std::string_view::npos || text.find('.', dot + 1) != std::string_view::npos) return std::nullopt;
  auto number = [](std::string_view part) -> std::optional<int> {
    int value = 0;
    if (part.empty()) return std::nullopt;
    auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
    if (error != std::errc{} || end != part.data() + part.size() || value < 0) return std::nullopt;
    return value;
  };
  auto major = number(text.substr(0, dot));
  auto minor = number(text.substr(dot + 1));
  if (!major || !minor) return std::nullopt;
  return ContractVersion{*major, *minor};
}

std::string ContractVersion::str() const { return std::to_string(major) + "." + std::to_string(minor); }

} // namespace octantpath::core
