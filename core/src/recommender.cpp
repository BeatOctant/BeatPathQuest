#include "octantpath/core/recommender.hpp"

namespace octantpath::core {

Recommender::Recommender(HostClient &client, HostConnection &connection, std::function<Settings()> settings, std::function<std::string()> language)
    : client_(client), connection_(connection), settings_(std::move(settings)), language_(std::move(language)) {}

bool Recommender::onStatus(ConnectionStatus const &status) {
  {
    std::lock_guard lock(mutex_);
    if (!cache_.needsReload(status)) return false;
  }
  return reloadPresets();
}

bool Recommender::reloadPresets() {
  auto ready = connection_.ready();
  std::lock_guard lock(mutex_);
  if (!ready) return cache_.update({.kind = HostResultKind::Unreachable, .detail = "not connected"});
  return cache_.update(client_.presets(ready->first, ready->second, language_(), settings_().hostRequestTimeout()));
}

std::optional<PresetsResponse> Recommender::presets() const {
  std::lock_guard lock(mutex_);
  return cache_.presets();
}

std::string Recommender::presetsFailure() const {
  std::lock_guard lock(mutex_);
  return cache_.failure();
}

std::optional<HostResult<RecommendationResponse>> Recommender::recommend(Preset const &preset, std::optional<ChartRef> const &seed, std::map<std::string, std::string> const &choices) {
  auto ready = connection_.ready();
  if (!ready) return std::nullopt;
  auto settings = settings_();
  auto request = preset_rules::request(preset, seed, choices, settings.mixDraftCount * 3);
  auto result = client_.recommend(ready->first, ready->second, request, settings.recommendationRequestTimeout());
  if (PresetsCache::needsReload(result)) reloadPresets();
  return result;
}

} // namespace octantpath::core
