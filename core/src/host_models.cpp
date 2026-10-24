#include "octantpath/core/host_models.hpp"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace octantpath::core {

namespace {

// 項目がない・null なら nullopt。型が違えば例外
template <class T> std::optional<T> optional(json const &object, char const *key) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) return std::nullopt;
  return it->get<T>();
}

} // namespace

HealthResponse parseHealth(std::string const &text) {
  auto object = json::parse(text);
  HealthResponse health;
  health.apiVersion = object.at("apiVersion").get<std::string>();
  health.hostVersion = optional<std::string>(object, "hostVersion");
  health.status = object.at("status").get<std::string>();
  health.reason = optional<std::string>(object, "reason");
  if (auto db = object.find("db"); db != object.end() && db->is_object()) {
    HealthDb parsed;
    parsed.dbVersion = optional<std::string>(*db, "dbVersion");
    parsed.genres = optional<std::vector<std::string>>(*db, "genres").value_or(std::vector<std::string>{});
    parsed.roles = optional<std::map<std::string, std::vector<std::string>>>(*db, "roles").value_or(std::map<std::string, std::vector<std::string>>{});
    health.db = std::move(parsed);
  }
  if (auto update = object.find("update"); update != object.end() && update->is_object()) health.updateReason = optional<std::string>(*update, "reason");
  health.paired = optional<bool>(object, "paired");
  health.instanceId = optional<std::string>(object, "instanceId");
  health.signals = optional<long long>(object, "signals");
  health.presetsRevision = optional<std::string>(object, "presetsRevision");
  return health;
}

PairResponse parsePair(std::string const &text) {
  auto object = json::parse(text);
  return {object.at("token").get<std::string>()};
}

ErrorDetail parseError(std::string const &text) {
  auto error = json::parse(text).at("error");
  ErrorDetail detail;
  detail.code = optional<std::string>(error, "code").value_or("");
  detail.message = optional<std::string>(error, "message").value_or("");
  detail.apiVersion = optional<std::string>(error, "apiVersion");
  return detail;
}

std::string toJson(PairRequest const &request) { return json{{"secret", request.secret}, {"name", request.name}}.dump(); }

std::map<std::string, std::string> viewRolesOf(std::map<std::string, std::vector<std::string>> const &roles) {
  std::map<std::string, std::string> result;
  for (auto const &[role, views] : roles)
    for (auto const &view : views) result.try_emplace(view, role);
  return result;
}

} // namespace octantpath::core
