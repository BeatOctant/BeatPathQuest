#include "fake_transport.hpp"

#include "octantpath/core/connection.hpp"
#include "octantpath/core/contract_version.hpp"
#include "octantpath/core/status_text.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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

struct Fixture {
  TempDir dir;
  FakeTransport transport;
  HostClient client{transport};
  ConnectionStore store{dir.path, nullptr};
  HostConnection connection{client, store, [] { return Settings{}; }, "Beat Saber (Quest)"};
};

} // namespace

TEST_CASE("契約の版を読む") {
  CHECK(ContractVersion::parse("1.2")->minor == 2);
  CHECK_FALSE(ContractVersion::parse("1"));
  CHECK_FALSE(ContractVersion::parse("1.2.3"));
  CHECK_FALSE(ContractVersion::parse("a.b"));
  CHECK_FALSE(ContractVersion::parse("-1.0"));
}

TEST_CASE("人が入れたつなぐ先を読む") {
  CHECK(parseAddress("192.168.0.10")->host == "192.168.0.10");
  CHECK_FALSE(parseAddress("192.168.0.10")->port);
  CHECK(parseAddress(" 192.168.0.10:23287 ")->port == 23287);
  CHECK(parseAddress("[fe80::1]:23287")->host == "fe80::1");
  CHECK(parseAddress("fe80::1")->host == "fe80::1");
  CHECK(parseAddress("::ffff:192.168.0.10")->host == "::ffff:192.168.0.10");
  CHECK_FALSE(parseAddress("[1:2]"));
  CHECK_FALSE(parseAddress("1:2:3:4:5:6:7:8:9"));
  CHECK_FALSE(parseAddress(""));
  CHECK_FALSE(parseAddress("http://192.168.0.10"));
  CHECK_FALSE(parseAddress("192.168.0.10:0"));
  CHECK_FALSE(parseAddress("192.168.0.10:99999"));
  CHECK_FALSE(parseAddress("user@host"));
}

TEST_CASE("接続情報がなければ既定のつなぐ先に health を送り、版のヘッダを付ける") {
  Fixture f;
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(R"({"apiVersion":"2.0","hostVersion":"0.1.3","status":"loading","db":null})");
  auto status = f.connection.refresh();
  CHECK(status.state == ConnectionState::NeedsPairing);
  REQUIRE(f.transport.requests.size() == 1);
  CHECK(f.transport.requests[0].first.host == "127.0.0.1");
  CHECK(f.transport.requests[0].first.port == 23287);
  CHECK(header(f.transport.requests[0].second, "OctantPath-Api-Version") == kApiVersion.str());
  CHECK(header(f.transport.requests[0].second, "Authorization").empty());
}

TEST_CASE("届かなければ Unreachable") {
  Fixture f;
  auto status = f.connection.refresh();
  CHECK(status.state == ConnectionState::Unreachable);
  CHECK(status.detail.find("refused") != std::string::npos);
  CHECK_FALSE(f.connection.ready());
}

TEST_CASE("ペアリングが通ればつなぐ先とトークンを書き、トークン付きで確かめ直す") {
  Fixture f;
  f.transport.responses["POST /v1/pair"] = FakeTransport::ok(example("pair.response.json"));
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(example("health.ready.json"));
  auto status = f.connection.pair(" 482913 ", parseAddress("192.168.0.10"));
  CHECK(status.state == ConnectionState::Ready);
  CHECK(status.presetsRevision == "2026.10.04-1");
  CHECK(status.viewRoles.at("meta_tags@2") == "meta");

  auto pairRequest = nlohmann::json::parse(f.transport.requests[0].second.body);
  CHECK(pairRequest["secret"] == "482913");
  CHECK(pairRequest["name"] == "Beat Saber (Quest)");
  CHECK(header(f.transport.requests[1].second, "Authorization") == "Bearer bc1_6Zt0Qm3vX9pL2sK8wR4yN7aH5dF1gJ0c");
  CHECK(f.transport.requests[1].first.host == "192.168.0.10");

  auto saved = f.store.load();
  CHECK(saved->host == "192.168.0.10");
  CHECK_FALSE(saved->port);
  CHECK(saved->token == "bc1_6Zt0Qm3vX9pL2sK8wR4yN7aH5dF1gJ0c");
  REQUIRE(f.connection.ready());
  CHECK(f.connection.ready()->second == "bc1_6Zt0Qm3vX9pL2sK8wR4yN7aH5dF1gJ0c");
}

TEST_CASE("ペアリングを断られたら接続情報を変えない") {
  Fixture f;
  f.store.save({.host = "10.0.0.2", .port = 9000, .token = "old"});
  f.transport.responses["POST /v1/pair"] = FakeTransport::ok(example("error.unauthorized.json"), 401);
  auto status = f.connection.pair("000000", parseAddress("192.168.0.10"));
  CHECK(status.state == ConnectionState::PairingRejected);
  auto saved = f.store.load();
  CHECK(saved->host == "10.0.0.2");
  CHECK(saved->token == "old");
}

TEST_CASE("同じ機器の既定のつなぐ先は空のまま覚える") {
  Fixture f;
  f.transport.responses["POST /v1/pair"] = FakeTransport::ok(example("pair.response.json"));
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(example("health.ready.json"));
  f.connection.pair("482913", parseAddress("127.0.0.1"));
  CHECK_FALSE(f.store.load()->host);
}

TEST_CASE("メジャー版が違えば ApiVersionMismatch で、更新する側を言う") {
  Fixture f;
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(R"({"apiVersion":"3.0","status":"ready"})");
  auto status = f.connection.refresh();
  CHECK(status.state == ConnectionState::ApiVersionMismatch);
  CHECK(describe(status, Language::Ja).find("OctantPath を更新") != std::string::npos);
}

TEST_CASE("426 の応答も版の食い違いとして扱う") {
  Fixture f;
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(example("error.version_mismatch.json"), 426);
  CHECK(f.connection.refresh().state == ConnectionState::ApiVersionMismatch);
}

TEST_CASE("壊れた接続情報は上書きせず LocalInfoUnreadable") {
  Fixture f;
  std::ofstream(f.dir.path / "connection.json") << "{ broken";
  CHECK(f.connection.refresh().state == ConnectionState::LocalInfoUnreadable);
  CHECK(f.connection.pair("482913", std::nullopt).state == ConnectionState::LocalInfoUnreadable);
  std::ifstream file(f.dir.path / "connection.json");
  std::string text((std::istreambuf_iterator<char>(file)), {});
  CHECK(text == "{ broken");
}

TEST_CASE("degraded と loading") {
  Fixture f;
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(R"({"apiVersion":"2.0","status":"degraded","reason":"schema_unsupported","paired":true})");
  auto status = f.connection.refresh();
  CHECK(status.state == ConnectionState::Degraded);
  CHECK(describe(status, Language::En) == "Recommendations are unavailable (OctantCompass: schema_unsupported)");
  f.transport.responses["GET /v1/health"] = FakeTransport::ok(R"({"apiVersion":"2.0","status":"loading","paired":true})");
  CHECK(f.connection.refresh().state == ConnectionState::Loading);
}

TEST_CASE("言語: auto は機器の言語") {
  CHECK(resolveLanguage("auto", "ja-JP") == Language::Ja);
  CHECK(resolveLanguage("auto", "en-US") == Language::En);
  CHECK(resolveLanguage("en", "ja-JP") == Language::En);
}

TEST_CASE("設定: 読めなければ既定値") {
  TempDir dir;
  std::ofstream(dir.path / "settings.json") << R"({"hostRequestTimeoutSeconds": 3, "environmentName": "Quest 3", "unknown": 1})";
  auto settings = loadSettings(dir.path, nullptr);
  CHECK(settings.hostRequestTimeoutSeconds == 3);
  CHECK(settings.environmentName == "Quest 3");
  std::ofstream(dir.path / "settings.json") << "nope";
  std::string warned;
  CHECK(loadSettings(dir.path, [&](std::string const &m) { warned = m; }).hostRequestTimeoutSeconds == 10);
  CHECK_FALSE(warned.empty());
}
