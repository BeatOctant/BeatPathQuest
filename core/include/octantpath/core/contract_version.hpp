#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace octantpath::core {

// 契約の版（"メジャー.マイナー"）。比べ方は OctantSpecs の「契約の版と食い違いの知らせ方」
struct ContractVersion {
  int major = 0;
  int minor = 0;

  static std::optional<ContractVersion> parse(std::string_view text);
  std::string str() const;
};

// OctantPath が話す契約3（OctantPath ↔ OctantCompass）の版
inline constexpr ContractVersion kApiVersion{2, 0};

} // namespace octantpath::core
