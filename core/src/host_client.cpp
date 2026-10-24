#include "octantpath/core/host_client.hpp"

#include "octantpath/core/contract_version.hpp"

#include <exception>

namespace octantpath::core {

template <class T, class Parse>
HostResult<T> HostClient::send(Endpoint const &endpoint, HttpRequest request, std::optional<std::string> const &token, std::chrono::milliseconds timeout, Parse parse) {
  request.headers.emplace_back(kApiVersionHeader, kApiVersion.str());
  if (token && !token->empty()) request.headers.emplace_back("Authorization", "Bearer " + *token);
  if (!request.body.empty()) request.headers.emplace_back("Content-Type", "application/json");

  auto response = transport_.send(endpoint, request, timeout);
  HostResult<T> result;
  result.status = response.status;
  if (!response.reached) {
    result.kind = HostResultKind::Unreachable;
    result.detail = response.error;
    return result;
  }
  if (response.status >= 200 && response.status < 300) {
    try {
      result.body = parse(response.body);
      result.kind = HostResultKind::Ok;
    } catch (std::exception const &e) {
      result.kind = HostResultKind::Malformed;
      result.detail = e.what();
    }
    return result;
  }
  result.kind = HostResultKind::Failed;
  try {
    result.error = parseError(response.body);
    result.detail = result.error->message;
  } catch (std::exception const &) {
    // 本文が契約の形でなければ、状態コードだけで扱う
  }
  return result;
}

HostResult<HealthResponse> HostClient::health(Endpoint const &endpoint, std::optional<std::string> const &token, std::chrono::milliseconds timeout) {
  return send<HealthResponse>(endpoint, {"GET", "/v1/health", {}, {}}, token, timeout, parseHealth);
}

HostResult<PairResponse> HostClient::pair(Endpoint const &endpoint, PairRequest const &body, std::chrono::milliseconds timeout) {
  return send<PairResponse>(endpoint, {"POST", "/v1/pair", {}, toJson(body)}, std::nullopt, timeout, parsePair);
}

HostResult<PresetsResponse> HostClient::presets(Endpoint const &endpoint, std::string const &token, std::string const &language, std::chrono::milliseconds timeout) {
  return send<PresetsResponse>(endpoint, {"GET", "/v1/presets", {}, {}}, token, timeout, [&](std::string const &text) { return parsePresets(text, language); });
}

HostResult<RecommendationResponse> HostClient::recommend(Endpoint const &endpoint, std::string const &token, RecommendationRequest const &body, std::chrono::milliseconds timeout) {
  return send<RecommendationResponse>(endpoint, {"POST", "/v1/recommendations", {}, toJson(body)}, token, timeout, parseRecommendations);
}

} // namespace octantpath::core
