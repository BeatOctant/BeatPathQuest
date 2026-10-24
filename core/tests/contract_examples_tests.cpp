#include "fake_transport.hpp"

#include "octantpath/core/host_models.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using namespace octantpath::core;

TEST_CASE("health.ready の例を読める") {
  auto health = parseHealth(example("health.ready.json"));
  CHECK(health.apiVersion == "2.0");
  CHECK(health.status == "ready");
  REQUIRE(health.db);
  CHECK(health.db->dbVersion == "2026.10.04");
  CHECK(health.db->roles.at("music") == std::vector<std::string>{"clap_htsat_unfused@1"});
  CHECK(health.paired == true);
  CHECK(health.instanceId == "4f1c2a9d8b7e6f50");
  CHECK(health.signals == 9);
  CHECK(health.presetsRevision == "2026.10.04-1");
  CHECK(viewRolesOf(health.db->roles).at("chart_patterns@2") == "chart");
}

TEST_CASE("health.degraded の例を読める（db は null）") {
  auto health = parseHealth(example("health.degraded.json"));
  CHECK(health.status == "degraded");
  CHECK(health.reason == "schema_unsupported");
  CHECK_FALSE(health.db);
  CHECK_FALSE(health.paired);
}

TEST_CASE("health.update_unsupported の例を読める") {
  auto health = parseHealth(example("health.update_unsupported.json"));
  CHECK(health.updateReason == "index_unsupported");
}

TEST_CASE("pair の例: 要求の形と応答") {
  auto expected = nlohmann::json::parse(example("pair.request.json"));
  auto written = nlohmann::json::parse(toJson({expected["secret"], expected["name"]}));
  CHECK(written == expected);
  CHECK(parsePair(example("pair.response.json")).token == "bc1_6Zt0Qm3vX9pL2sK8wR4yN7aH5dF1gJ0c");
}

TEST_CASE("error の例を読める") {
  CHECK(parseError(example("error.unauthorized.json")).code == "unauthorized");
  CHECK(parseError(example("error.unknown_preset.json")).code == "unknown_preset");
  auto mismatch = parseError(example("error.version_mismatch.json"));
  CHECK(mismatch.code == "version_mismatch");
  CHECK(mismatch.apiVersion == "2.0");
}
