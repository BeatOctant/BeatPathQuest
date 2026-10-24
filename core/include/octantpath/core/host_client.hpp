#pragma once

#include "octantpath/core/host_models.hpp"
#include "octantpath/core/preset_models.hpp"
#include "octantpath/core/recommendations.hpp"
#include "octantpath/core/transport.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace octantpath::core {

enum class HostResultKind { Ok, Unreachable, Failed, Malformed };

// 契約3 の要求の結果。HTTP の失敗は例外にせず、ここで返す
template <class T> struct HostResult {
  HostResultKind kind = HostResultKind::Unreachable;
  std::optional<T> body;
  int status = 0;
  std::optional<ErrorDetail> error;
  // OctantCompass の message か、届かなかった・読めなかった理由
  std::string detail;

  bool ok() const { return kind == HostResultKind::Ok; }
  bool isUnauthorized() const { return kind == HostResultKind::Failed && (status == 401 || (error && error->code == "unauthorized")); }
  bool isUnknownPreset() const { return kind == HostResultKind::Failed && error && error->code == "unknown_preset"; }
  bool isVersionMismatch() const { return kind == HostResultKind::Failed && (status == 426 || (error && error->code == "version_mismatch")); }
  // つなぐ先を忘れる失敗: トークンが通らない・版が違う・届かない
  bool losesConnection() const { return isUnauthorized() || isVersionMismatch() || kind == HostResultKind::Unreachable; }
  // 接続の状態を確かめ直すべき失敗: 上の 3 つと、OctantCompass の準備ができていない（503）
  bool shouldRecheck() const { return losesConnection() || status == 503; }
  // 画面に出す詳しい理由。detail がなければ HTTP の状態コード
  std::string detailOrStatus() const { return detail.empty() ? "HTTP " + std::to_string(status) : detail; }
};

// 契約3（OctantPath ↔ OctantCompass）の MOD 側
class HostClient {
public:
  static constexpr char const *kApiVersionHeader = "OctantPath-Api-Version";

  explicit HostClient(Transport &transport) : transport_(transport) {}

  HostResult<HealthResponse> health(Endpoint const &endpoint, std::optional<std::string> const &token, std::chrono::milliseconds timeout);
  HostResult<PairResponse> pair(Endpoint const &endpoint, PairRequest const &body, std::chrono::milliseconds timeout);
  // language は表示名を選ぶ言語（"ja" や "en"）
  HostResult<PresetsResponse> presets(Endpoint const &endpoint, std::string const &token, std::string const &language, std::chrono::milliseconds timeout);
  HostResult<RecommendationResponse> recommend(Endpoint const &endpoint, std::string const &token, RecommendationRequest const &body, std::chrono::milliseconds timeout);

private:
  template <class T, class Parse>
  HostResult<T> send(Endpoint const &endpoint, HttpRequest request, std::optional<std::string> const &token, std::chrono::milliseconds timeout, Parse parse);

  Transport &transport_;
};

} // namespace octantpath::core
