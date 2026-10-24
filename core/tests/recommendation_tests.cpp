#include "fake_transport.hpp"

#include "octantpath/core/connection.hpp"
#include "octantpath/core/presets.hpp"
#include "octantpath/core/recommender.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <random>

using namespace octantpath::core;

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() / ("octantpath-core-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() { std::filesystem::remove_all(path); }
};

// ペアリング済みで ready のつなぐ先
struct Fixture {
  TempDir dir;
  FakeTransport transport;
  HostClient client{transport};
  ConnectionStore store{dir.path, nullptr};
  HostConnection connection{client, store, [] { return Settings{}; }, "Beat Saber (Quest)"};
  Recommender recommender{client, connection, [] { return Settings{}; }, [] { return std::string("ja"); }};

  Fixture() {
    transport.responses["POST /v1/pair"] = FakeTransport::ok(example("pair.response.json"));
    transport.responses["GET /v1/health"] = FakeTransport::ok(example("health.ready.json"));
    transport.responses["GET /v1/presets"] = FakeTransport::ok(example("presets.response.json"));
    connection.pair("482913", std::nullopt);
    transport.requests.clear();
  }
};

Preset const &presetById(PresetsResponse const &presets, std::string const &id) {
  for (auto const &p : presets.presets)
    if (p.id == id) return p;
  throw std::runtime_error("no preset " + id);
}

} // namespace

TEST_CASE("presets の例を読める") {
  auto presets = parsePresets(example("presets.response.json"), "ja");
  CHECK(presets.revision == "2026.10.04-1");
  REQUIRE(presets.presets.size() == 6);
  CHECK(presets.presets[0].id == "all");
  CHECK(presets.presets[0].label == "すべて");
  auto genre = presetById(presets, "genre");
  REQUIRE(genre.inputs.size() == 1);
  CHECK(genre.inputs[0].kind == "choice");
  CHECK(genre.inputs[0].options.size() == 2);
  CHECK(presetById(presets, "challenge").inputs[0].optional);
}

TEST_CASE("表示名が {言語: 文字列} なら今の言語、なければ英語、なければ最初のもの") {
  auto text = R"({"revision":"r","presets":[
    {"id":"a","label":{"ja":"すべて","en":"All"},"inputs":[]},
    {"id":"b","label":{"fr":"Tout","en":"All"},"inputs":[]},
    {"id":"c","label":{"fr":"Tout"},"inputs":[]}]})";
  auto presets = parsePresets(text, "ja");
  CHECK(presets.presets[0].label == "すべて");
  CHECK(presets.presets[1].label == "All");
  CHECK(presets.presets[2].label == "Tout");
}

TEST_CASE("手札の出し方: 起点のない場所では起点が必須のものを出さない") {
  auto presets = parsePresets(example("presets.response.json"), "ja").presets;
  auto ids = [](std::vector<Preset> const &shown) {
    std::vector<std::string> result;
    for (auto const &p : shown) result.push_back(p.id);
    return result;
  };
  CHECK(ids(preset_rules::shown(presets, true)) == std::vector<std::string>{"all", "song", "chart", "genre", "challenge", "again"});
  CHECK(ids(preset_rules::shown(presets, false)) == std::vector<std::string>{"genre", "challenge", "again"});

  Preset unknown{.id = "x", .label = "X", .inputs = {{.kind = "mood"}}};
  CHECK_FALSE(preset_rules::isShown(unknown, true));
  Preset noOptions{.id = "y", .label = "Y", .inputs = {{.kind = "choice", .name = "genre"}}};
  CHECK_FALSE(preset_rules::isShown(noOptions, true));
}

TEST_CASE("手札の要求は id と入力と件数だけで、起点は使う手札にだけ付ける") {
  auto presets = parsePresets(example("presets.response.json"), "ja");
  ChartRef seed{"1111111111111111111111111111111111111111", "Standard", "Expert"};

  auto genre = nlohmann::json::parse(toJson(preset_rules::request(presetById(presets, "genre"), seed, {{"genre", "jpop"}}, 30)));
  CHECK(genre == nlohmann::json::parse(R"({"preset":"genre","choices":{"genre":"jpop"},"count":30})"));

  auto song = nlohmann::json::parse(toJson(preset_rules::request(presetById(presets, "song"), seed, {}, 30)));
  CHECK(song == nlohmann::json::parse(R"({"preset":"song","seed":{"hash":"1111111111111111111111111111111111111111","characteristic":"Standard","difficulty":"Expert"},"count":30})"));

  ChartRef upper{"8DFC3BAEFA47765DDA2918619E772FB01ACDA858", "Standard", "Hard"};
  CHECK(preset_rules::request(presetById(presets, "song"), upper, {}, 30).seed->hash == "8dfc3baefa47765dda2918619e772fb01acda858");

  // 例の要求と同じ形（params と exclude は今は送らない）
  auto expected = nlohmann::json::parse(example("recommendations.request.preset.json"));
  CHECK(genre["preset"] == expected["preset"]);
  CHECK(genre["choices"] == expected["choices"]);
}

TEST_CASE("recommendations の例を読める（順は変えない）") {
  auto response = parseRecommendations(example("recommendations.response.json"));
  CHECK(response.requestId == "r-20261004-000001");
  CHECK(response.dbVersion == "2026.10.04");
  CHECK(response.aims.at("Standard") == "normal");
  REQUIRE(response.candidates.size() == 2);
  CHECK(response.candidates[0].rank == 1);
  CHECK(response.candidates[0].hash == "2222222222222222222222222222222222222222");
  CHECK(response.candidates[0].difficulty == "Hard");
  REQUIRE(response.candidates[0].reason.difficulty);
  CHECK(response.candidates[0].reason.difficulty->clearChance == doctest::Approx(0.83));
  CHECK(response.candidates[0].reason.difficulty->nearestStrong->difficulty == "Expert");
  CHECK(response.candidates[1].characteristic == "OneSaber");
  CHECK_FALSE(response.candidates[1].reason.difficulty);
}

TEST_CASE("ready になったら一覧を取り、revision が変わるまで取り直さない") {
  Fixture f;
  auto status = f.connection.refresh();
  CHECK(f.recommender.onStatus(status));
  REQUIRE(f.recommender.presets());
  CHECK(f.recommender.presets()->presets[0].label == "すべて");
  CHECK(header(f.transport.requests.back().second, "Authorization") == "Bearer bc1_6Zt0Qm3vX9pL2sK8wR4yN7aH5dF1gJ0c");

  f.transport.requests.clear();
  CHECK_FALSE(f.recommender.onStatus(status));
  CHECK(f.transport.requests.empty());

  status.presetsRevision = "2026.10.05-1";
  CHECK(f.recommender.onStatus(status));
}

TEST_CASE("一覧を取り直せなくても前の一覧を使い続ける") {
  Fixture f;
  auto status = f.connection.refresh();
  f.recommender.onStatus(status);
  f.transport.responses["GET /v1/presets"] = FakeTransport::ok(example("error.json"), 500);
  status.presetsRevision = "2026.10.05-1";
  CHECK_FALSE(f.recommender.onStatus(status));
  REQUIRE(f.recommender.presets());
  CHECK(f.recommender.presets()->revision == "2026.10.04-1");
  CHECK_FALSE(f.recommender.presetsFailure().empty());
}

TEST_CASE("推薦を頼むと設定の 3 倍の件数で送り、unknown_preset なら一覧を取り直す") {
  Fixture f;
  f.recommender.onStatus(f.connection.refresh());
  auto presets = *f.recommender.presets();
  f.transport.responses["POST /v1/recommendations"] = FakeTransport::ok(example("recommendations.response.json"));
  auto result = f.recommender.recommend(presetById(presets, "again"), std::nullopt, {});
  REQUIRE(result);
  REQUIRE(result->ok());
  CHECK(result->body->candidates.size() == 2);
  auto sent = nlohmann::json::parse(f.transport.requests.back().second.body);
  CHECK(sent["count"] == 30);
  CHECK(sent["preset"] == "again");
  CHECK_FALSE(sent.contains("seed"));

  f.transport.requests.clear();
  f.transport.responses["POST /v1/recommendations"] = FakeTransport::ok(example("error.unknown_preset.json"), 400);
  result = f.recommender.recommend(presetById(presets, "again"), std::nullopt, {});
  CHECK(result->isUnknownPreset());
  REQUIRE(f.transport.requests.size() == 2);
  CHECK(f.transport.requests[1].second.path == "/v1/presets");
}

TEST_CASE("つないでいなければ推薦を頼まない") {
  TempDir dir;
  FakeTransport transport;
  HostClient client{transport};
  ConnectionStore store{dir.path, nullptr};
  HostConnection connection{client, store, [] { return Settings{}; }, "Beat Saber (Quest)"};
  Recommender recommender{client, connection, [] { return Settings{}; }, [] { return std::string("ja"); }};
  CHECK_FALSE(recommender.recommend(Preset{.id = "again", .label = "久しぶり"}, std::nullopt, {}));
  CHECK(transport.requests.empty());
}
