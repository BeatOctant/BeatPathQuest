#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace octantpath::core {

// 譜面の識別（契約3: hash + characteristic + difficulty）
struct ChartRef {
  std::string hash;
  std::optional<std::string> characteristic;
  std::optional<std::string> difficulty;
};

struct ExcludeEntry {
  std::string hash;
  std::optional<std::string> characteristic;
  std::optional<std::string> difficulty;
  std::optional<std::string> reason;
};

// POST /v1/recommendations の要求。プリセットの要求では、重み・狙い（difficulty）は送らない
struct RecommendationRequest {
  std::optional<std::string> preset;
  std::map<std::string, std::string> choices;
  std::optional<ChartRef> seed;
  int count = 0;
  std::vector<ExcludeEntry> exclude;
};

struct DifficultyReason {
  std::optional<std::string> aim;
  std::optional<double> distance;
  std::optional<double> target;
  std::optional<double> clearChance;
  std::optional<ChartRef> nearestStrong;
};

struct CandidateReason {
  std::map<std::string, double> views;
  std::optional<DifficultyReason> difficulty;
};

struct Candidate {
  int rank = 0;
  std::string hash;
  std::string characteristic;
  std::string difficulty;
  CandidateReason reason;
};

// POST /v1/recommendations の応答。候補の順は OctantCompass が返した順のまま使う（憲法の原則 6）
struct RecommendationResponse {
  std::string requestId;
  std::optional<std::string> dbVersion;
  std::map<std::string, std::string> aims;
  std::vector<std::string> ignoredWeights;
  std::vector<Candidate> candidates;
};

std::string toJson(RecommendationRequest const &request);
RecommendationResponse parseRecommendations(std::string const &text);

} // namespace octantpath::core
