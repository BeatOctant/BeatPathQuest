#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace octantpath::core {

// 契約3（OctantPath ↔ OctantCompass）の GET /v1/health の応答。知らない項目は無視する
struct HealthDb {
  std::optional<std::string> dbVersion;
  std::vector<std::string> genres;
  // 役割名ごとのビュー（契約3 の /v1/health の db.roles）。なければ空
  std::map<std::string, std::vector<std::string>> roles;
};

struct HealthResponse {
  std::string apiVersion;
  std::optional<std::string> hostVersion;
  std::string status;
  std::optional<std::string> reason;
  std::optional<HealthDb> db;
  std::optional<std::string> updateReason;
  std::optional<bool> paired;
  // この OctantCompass の起動を表す値と、この環境の signal の件数。ペアリング済みのときだけ返る（契約3 2.0）
  std::optional<std::string> instanceId;
  std::optional<long long> signals;
  // /v1/presets の revision。ペアリング済みで ready のときだけ返る
  std::optional<std::string> presetsRevision;
};

struct PairRequest {
  std::string secret;
  std::string name;
};

struct PairResponse {
  std::string token;
};

struct ErrorDetail {
  std::string code;
  std::string message;
  std::optional<std::string> apiVersion;
};

namespace health_status {
inline constexpr char const *kLoading = "loading";
inline constexpr char const *kReady = "ready";
inline constexpr char const *kDegraded = "degraded";
} // namespace health_status

// JSON の文字列から読む。形が合わなければ例外（nlohmann::json の例外）を投げる
HealthResponse parseHealth(std::string const &text);
PairResponse parsePair(std::string const &text);
ErrorDetail parseError(std::string const &text);
std::string toJson(PairRequest const &request);

// db.roles（役割名 → ビューの一覧）を、ビュー → 役割名に裏返す。1 つのビューが 2 つの役割に出ることは契約にないので、先のものを使う
std::map<std::string, std::string> viewRolesOf(std::map<std::string, std::vector<std::string>> const &roles);

} // namespace octantpath::core
