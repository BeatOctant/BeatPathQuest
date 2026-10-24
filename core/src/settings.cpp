#include "octantpath/core/settings.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace octantpath::core {

std::string Settings::deletionProblem() const {
  if (keepMinPlays < 1) return "keepMinPlays（" + std::to_string(keepMinPlays) + "）が 1 より小さいので、何も消さない";
  if (trialExpiryLaunchDays < 1) return "trialExpiryLaunchDays（" + std::to_string(trialExpiryLaunchDays) + "）が 1 より小さいので、何も消さない";
  if (!(trialMaxMegabytes > 0) || std::isinf(trialMaxMegabytes)) return "trialMaxMegabytes が正の有限の数ではないので、何も消さない";
  return {};
}

Settings loadSettings(std::filesystem::path const &directory, std::function<void(std::string const &)> const &warn) {
  Settings settings;
  auto path = directory / "settings.json";
  std::error_code exists;
  if (!std::filesystem::exists(path, exists)) return settings;
  try {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    auto object = nlohmann::json::parse(text.str());
    auto read = [&](char const *key, auto &value) {
      if (auto it = object.find(key); it != object.end() && !it->is_null()) value = it->get<std::remove_reference_t<decltype(value)>>();
    };
    read("hostRequestTimeoutSeconds", settings.hostRequestTimeoutSeconds);
    read("healthRetryIntervalSeconds", settings.healthRetryIntervalSeconds);
    read("recommendationRequestTimeoutSeconds", settings.recommendationRequestTimeoutSeconds);
    read("mixDraftCount", settings.mixDraftCount);
    read("beatSaverApiUrl", settings.beatSaverApiUrl);
    read("beatSaverApiIntervalSeconds", settings.beatSaverApiIntervalSeconds);
    read("beatSaverApiMaxBackoffSeconds", settings.beatSaverApiMaxBackoffSeconds);
    read("beatSaverProxyHost", settings.beatSaverProxyHost);
    read("beatSaverProxyPort", settings.beatSaverProxyPort);
    read("cdnTimeoutSeconds", settings.cdnTimeoutSeconds);
    read("zipMaxDownloadMegabytes", settings.zipMaxDownloadMegabytes);
    read("zipMaxEntries", settings.zipMaxEntries);
    read("zipMaxExtractedMegabytes", settings.zipMaxExtractedMegabytes);
    read("environmentName", settings.environmentName);
    read("language", settings.language);
    read("keepMinPlays", settings.keepMinPlays);
    read("trialExpiryLaunchDays", settings.trialExpiryLaunchDays);
    read("trialMaxMegabytes", settings.trialMaxMegabytes);
    read("deleteDryRun", settings.deleteDryRun);
  } catch (std::exception const &e) {
    if (warn) warn(std::string("settings.json could not be read: ") + e.what());
    Settings fallback;
    fallback.unreadable = e.what();
    return fallback;
  }
  return settings;
}

} // namespace octantpath::core
