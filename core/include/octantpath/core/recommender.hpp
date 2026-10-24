#pragma once

#include "octantpath/core/connection.hpp"
#include "octantpath/core/host_client.hpp"
#include "octantpath/core/presets.hpp"
#include "octantpath/core/settings.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace octantpath::core {

// 手札の一覧を持ち、推薦を頼む。画面のスレッドの外から呼ぶ
class Recommender {
public:
  // language は手札の表示名を選ぶ言語（"ja" や "en"）を返す
  Recommender(HostClient &client, HostConnection &connection, std::function<Settings()> settings, std::function<std::string()> language);

  // 接続を確かめたあとに呼ぶ。一覧が要るときだけ取り直す。取り直したら true
  bool onStatus(ConnectionStatus const &status);

  // 最後に取れた一覧（取り直しに失敗しても前のもの）と、取れなかった理由
  std::optional<PresetsResponse> presets() const;
  std::string presetsFailure() const;

  // 押された手札で推薦を頼む。件数は設定の mixDraftCount の 3 倍。推薦が使えるつなぐ先がなければ nullopt。
  // 今の一覧にない手札だった（unknown_preset）ときは、一覧を取り直す
  std::optional<HostResult<RecommendationResponse>> recommend(Preset const &preset, std::optional<ChartRef> const &seed, std::map<std::string, std::string> const &choices);

private:
  bool reloadPresets();

  HostClient &client_;
  HostConnection &connection_;
  std::function<Settings()> settings_;
  std::function<std::string()> language_;

  mutable std::mutex mutex_;
  PresetsCache cache_;
};

} // namespace octantpath::core
