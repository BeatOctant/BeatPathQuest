#include "octantpath/core/recommendations.hpp"

#include <nlohmann/json.hpp>

namespace octantpath::core {

namespace {

using nlohmann::json;

template <class T> std::optional<T> optionalOf(json const &object, char const *key) {
  if (auto it = object.find(key); it != object.end() && !it->is_null()) return it->get<T>();
  return std::nullopt;
}

json chartJson(ChartRef const &chart) {
  json object{{"hash", chart.hash}};
  if (chart.characteristic) object["characteristic"] = *chart.characteristic;
  if (chart.difficulty) object["difficulty"] = *chart.difficulty;
  return object;
}

ChartRef chartOf(json const &object) {
  return {object.at("hash").get<std::string>(), optionalOf<std::string>(object, "characteristic"), optionalOf<std::string>(object, "difficulty")};
}

} // namespace

std::string toJson(RecommendationRequest const &request) {
  json object{{"count", request.count}};
  if (request.preset) object["preset"] = *request.preset;
  if (!request.choices.empty()) object["choices"] = request.choices;
  if (request.seed) object["seed"] = chartJson(*request.seed);
  if (!request.exclude.empty()) {
    auto &exclude = object["exclude"] = json::array();
    for (auto const &entry : request.exclude) {
      json item{{"hash", entry.hash}};
      if (entry.characteristic) item["characteristic"] = *entry.characteristic;
      if (entry.difficulty) item["difficulty"] = *entry.difficulty;
      if (entry.reason) item["reason"] = *entry.reason;
      exclude.push_back(std::move(item));
    }
  }
  return object.dump();
}

RecommendationResponse parseRecommendations(std::string const &text) {
  auto object = json::parse(text);
  RecommendationResponse response;
  response.requestId = object.value("requestId", "");
  response.dbVersion = optionalOf<std::string>(object, "dbVersion");
  if (auto aims = object.find("aims"); aims != object.end() && aims->is_object()) response.aims = aims->get<std::map<std::string, std::string>>();
  if (auto ignored = object.find("ignoredWeights"); ignored != object.end() && ignored->is_array()) response.ignoredWeights = ignored->get<std::vector<std::string>>();
  for (auto const &item : object.at("candidates")) {
    Candidate candidate;
    candidate.rank = item.at("rank").get<int>();
    candidate.hash = item.at("hash").get<std::string>();
    candidate.characteristic = item.at("characteristic").get<std::string>();
    candidate.difficulty = item.at("difficulty").get<std::string>();
    if (auto reason = item.find("reason"); reason != item.end() && reason->is_object()) {
      if (auto views = reason->find("views"); views != reason->end() && views->is_object()) candidate.reason.views = views->get<std::map<std::string, double>>();
      if (auto difficulty = reason->find("difficulty"); difficulty != reason->end() && difficulty->is_object()) {
        DifficultyReason d;
        d.aim = optionalOf<std::string>(*difficulty, "aim");
        d.distance = optionalOf<double>(*difficulty, "distance");
        d.target = optionalOf<double>(*difficulty, "target");
        d.clearChance = optionalOf<double>(*difficulty, "clearChance");
        if (auto nearest = difficulty->find("nearestStrong"); nearest != difficulty->end() && nearest->is_object()) d.nearestStrong = chartOf(*nearest);
        candidate.reason.difficulty = d;
      }
    }
    response.candidates.push_back(std::move(candidate));
  }
  return response;
}

} // namespace octantpath::core
