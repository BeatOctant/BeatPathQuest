#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace octantpath::core {

// 選曲画面の板を、つかんで置いた場所（ワールド座標の位置と向き）
struct PanelPlacement {
  std::array<float, 3> position{};
  // クォータニオン（x, y, z, w）
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

class PanelPlacementStore {
public:
  static constexpr char const *kFileName = "panel.json";

  PanelPlacementStore(std::filesystem::path directory, std::function<void(std::string const &)> warn);

  // ファイルがない、読めない、数でない値があるときは nullopt（決まった位置に出す）
  std::optional<PanelPlacement> load() const;
  // 書きかけのファイルを残さないよう、別のファイルに書いてから置き換える
  void save(PanelPlacement const &placement) const;

private:
  std::filesystem::path path_;
  std::function<void(std::string const &)> warn_;
};

} // namespace octantpath::core
