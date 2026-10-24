#pragma once

#include "octantpath/core/connection.hpp"
#include "octantpath/core/host_client.hpp"
#include "octantpath/core/preset_models.hpp"
#include "octantpath/core/recommendations.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace octantpath::core {

// 一覧をボタンにするときの決まり（PC 版の PresetRules と同じ）。並びは OctantCompass の返した順のまま
namespace preset_rules {
// 知らない種類の必須の入力があるものは出さない。起点のない場所では、起点が必須のものを出さない
bool isShown(Preset const &preset, bool hasSeedContext);
std::vector<Preset> shown(std::vector<Preset> const &presets, bool hasSeedContext);
bool needsSeed(Preset const &preset);
bool usesSeed(Preset const &preset);
// 押したあとに選ばせる入力（必須の choice）
std::vector<PresetInput> choices(Preset const &preset);
// 押されたときの要求。送るのは id と入力と件数だけ（重み・狙い・ジャンルの語は送らない）
RecommendationRequest request(Preset const &preset, std::optional<ChartRef> const &seed, std::map<std::string, std::string> const &choices, int count);
} // namespace preset_rules

// OctantCompass の手札の一覧と、取り直す決まり（PC 版の PresetsCache と同じ）
class PresetsCache {
public:
  // 最後に取れた一覧。取り直しに失敗しても前のものを使い続ける
  std::optional<PresetsResponse> const &presets() const { return presets_; }
  // 最後に取り直したときに取れなかった理由。取れたら空
  std::string const &failure() const { return failure_; }

  // 接続を確かめたあと、一覧を取り直すか。まだ取っていないときと、revision が変わったときだけ
  bool needsReload(ConnectionStatus const &status) const;
  // 推薦の要求が失敗したあと、一覧を取り直すか。今の一覧にない手札を名指しした（unknown_preset）ときだけ
  template <class T> static bool needsReload(HostResult<T> const &result) { return !result.shouldRecheck() && result.isUnknownPreset(); }

  // 取れたら true
  bool update(HostResult<PresetsResponse> const &result);

private:
  std::optional<PresetsResponse> presets_;
  std::string failure_;
};

} // namespace octantpath::core
