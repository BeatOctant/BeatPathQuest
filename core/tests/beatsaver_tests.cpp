#include "fake_transport.hpp"

#include "octantpath/core/beatsaver.hpp"

#include <doctest/doctest.h>

#include <cstdio>

using namespace octantpath::core;

namespace {

std::string const kHashA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
std::string const kHashB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

std::string mapJson(std::string const &key, std::string const &hash) {
  return R"({"id":")" + key + R"(","name":"Song )" + key + R"(","metadata":{"songName":"Song","songAuthorName":"Artist","levelAuthorName":"Mapper","duration":120},
  "versions":[{"hash":")" + hash + R"(","state":"Published","downloadURL":"https://r2cdn.beatsaver.com/)" + hash + R"(.zip","coverURL":"https://cdn.beatsaver.com/)" + hash + R"(.jpg","previewURL":"https://cdn.beatsaver.com/)" + hash + R"(.mp3",
  "diffs":[{"characteristic":"Standard","difficulty":"Expert","nps":5.5,"njs":18}]}]})";
}

std::string proxyPath(std::string const &hashes) { return "/v1/beatsaver?url=" + percentEncode("https://api.beatsaver.com/maps/hash/" + hashes); }

struct Fixture {
  FakeTransport transport;
  std::vector<std::chrono::milliseconds> slept;
  BeatSaverClient client{transport, [] { return Settings{}; }, [this](std::chrono::milliseconds d) { slept.push_back(d); }};
};

} // namespace

TEST_CASE("hash は 40 桁の 16 進だけを小文字にそろえる") {
  CHECK(normalizeHash(std::string(40, 'A')) == std::string(40, 'a'));
  CHECK_FALSE(normalizeHash("abc"));
  CHECK_FALSE(normalizeHash(std::string(40, 'g')));
}

TEST_CASE("BeatSaver の URL は https で beatsaver.com かその下だけ") {
  CHECK(allowedBeatSaverUrl("https://beatsaver.com/maps"));
  CHECK(allowedBeatSaverUrl("https://r2cdn.beatsaver.com/a.zip"));
  CHECK(allowedBeatSaverUrl("https://CDN.BeatSaver.com./a.jpg"));
  CHECK_FALSE(allowedBeatSaverUrl("http://cdn.beatsaver.com/a.jpg"));
  CHECK_FALSE(allowedBeatSaverUrl("https://evilbeatsaver.com/a"));
  CHECK_FALSE(allowedBeatSaverUrl("https://beatsaver.com.evil.com/a"));
  CHECK_FALSE(allowedBeatSaverUrl("https://user@cdn.beatsaver.com/a"));
}

TEST_CASE("頼んだ hash の版だけを返し、ほかの版で代用しない") {
  auto map = parseBeatSaverMap(mapJson("1a", kHashA));
  REQUIRE(map.version(std::string(40, 'A')));
  CHECK(map.version(kHashA)->diffs.at(0).nps == 5.5);
  CHECK_FALSE(map.version(kHashB));
}

TEST_CASE("1 件だけ頼んだときはマップがそのまま返る") {
  Fixture f;
  f.transport.responses["GET " + proxyPath(kHashA)] = FakeTransport::ok(mapJson("1a", kHashA));
  auto lookup = f.client.mapsByHash({std::string(40, 'A')});
  CHECK(lookup.error.empty());
  REQUIRE(lookup.maps.count(kHashA));
  CHECK(lookup.maps.at(kHashA).key == "1a");
  REQUIRE(f.transport.requests.size() == 1);
  CHECK(f.transport.requests[0].first.host == "127.0.0.1");
  CHECK(f.transport.requests[0].first.port == 23288);
}

TEST_CASE("何件か頼めば hash ごとの辞書で、見つからない hash は入らない") {
  Fixture f;
  f.transport.responses["GET " + proxyPath(kHashA + "," + kHashB)] = FakeTransport::ok(R"({")" + kHashA + R"(":)" + mapJson("1a", kHashA) + R"(,")" + kHashB + R"(":null})");
  auto lookup = f.client.mapsByHash({kHashA, kHashB, kHashA, "broken"});
  CHECK(lookup.maps.size() == 1);
  CHECK(lookup.maps.count(kHashA));
}

TEST_CASE("50 件ずつ分けて、間隔を空けて送る") {
  Fixture f;
  std::vector<std::string> hashes;
  for (int i = 0; i < 51; ++i) {
    char buffer[41];
    std::snprintf(buffer, sizeof buffer, "%040x", i);
    hashes.push_back(buffer);
  }
  f.client.mapsByHash(hashes);
  CHECK(f.transport.requests.size() == 2);
  CHECK_FALSE(f.slept.empty());
}

TEST_CASE("BeatSaver の 404 は見つからない、プロキシに届かなければ理由を返す") {
  Fixture f;
  f.transport.responses["GET " + proxyPath(kHashA)] = FakeTransport::ok(R"({"error":"Not Found"})", 404);
  auto lookup = f.client.mapsByHash({kHashA});
  CHECK(lookup.maps.empty());
  CHECK(lookup.error.empty());
  auto unreachable = f.client.mapsByHash({kHashB});
  CHECK(unreachable.error.find("proxy") != std::string::npos);
}

TEST_CASE("429 には間隔を倍にして再試行し、上限で止める") {
  Fixture f;
  f.transport.responses["GET " + proxyPath(kHashA)] = FakeTransport::ok("", 429);
  auto lookup = f.client.mapsByHash({kHashA});
  CHECK(lookup.error.find("too many") != std::string::npos);
  CHECK(f.transport.requests.size() == 6);
}

TEST_CASE("間隔と最大の待ちは 1 秒より短くしない") {
  FakeTransport transport;
  std::vector<std::chrono::milliseconds> slept;
  Settings settings;
  settings.beatSaverApiIntervalSeconds = 0;
  settings.beatSaverApiMaxBackoffSeconds = 0;
  BeatSaverClient client{transport, [settings] { return settings; }, [&](std::chrono::milliseconds d) { slept.push_back(d); }};
  transport.responses["GET " + proxyPath(kHashA)] = FakeTransport::ok("", 429);
  auto lookup = client.mapsByHash({kHashA});
  // 0 のままなら、429 の再試行は間隔が延びずに止まらない。最大の待ちも 1 秒なので、1 回目の 429 で止まる
  CHECK(lookup.error.find("too many") != std::string::npos);
  CHECK(transport.requests.size() == 1);
  transport.responses["GET " + proxyPath(kHashB)] = FakeTransport::ok(R"({"error":"Not Found"})", 404);
  client.mapsByHash({kHashB});
  REQUIRE_FALSE(slept.empty());
  CHECK(slept.front() > std::chrono::milliseconds(900));
}

TEST_CASE("候補に BeatSaver の情報を付け、推薦された版がなければ出さない") {
  RecommendationResponse response;
  response.candidates = {{1, std::string(40, 'A'), "Standard", "Expert", {}}, {2, kHashB, "Standard", "Hard", {}}, {3, "broken", "Standard", "Easy", {}}};
  BeatSaverClient::Lookup lookup;
  lookup.maps[kHashA] = parseBeatSaverMap(mapJson("1a", kHashA));
  lookup.maps[kHashB] = parseBeatSaverMap(mapJson("1b", kHashA));
  auto items = recommendationItems(response, lookup);
  REQUIRE(items.size() == 3);
  CHECK(items[0].shown());
  CHECK(items[0].nps == 5.5);
  CHECK(items[0].njs == 18);
  CHECK(items[1].map);
  CHECK_FALSE(items[1].shown());
  CHECK(items[2].hash.empty());
  CHECK_FALSE(items[2].shown());
}

TEST_CASE("CDN のファイルは BeatSaver の URL だけ、プロキシ越しに取る") {
  Fixture f;
  auto cover = "https://cdn.beatsaver.com/" + kHashA + ".jpg";
  f.transport.responses["GET /v1/beatsaver?url=" + percentEncode(cover)] = FakeTransport::ok("JPEG");
  CHECK(f.client.download(cover) == "JPEG");
  std::string error;
  CHECK_FALSE(f.client.download("https://example.com/a.jpg", &error));
  CHECK(error.find("not a BeatSaver URL") != std::string::npos);
  CHECK(f.transport.requests.size() == 1);
  CHECK_FALSE(f.client.download("https://cdn.beatsaver.com/missing.jpg", &error));
  CHECK(error.find("proxy") != std::string::npos);
}

TEST_CASE("zip は PC の zip のフォルダにあればそこから、使っていなければ BeatSaver から") {
  Fixture f;
  auto version = parseBeatSaverMap(mapJson("1a", kHashA)).versions.at(0);
  f.transport.responses["GET /v1/local-zip/" + kHashA] = FakeTransport::ok("LOCAL");
  CHECK(f.client.mapZip(std::string(40, 'A'), &version) == "LOCAL");
  // zip は設定の上限まで受け取る
  CHECK(f.transport.requests.back().second.maxResponseBytes == static_cast<size_t>(Settings{}.zipMaxDownloadMegabytes) * 1024 * 1024);

  f.transport.responses["GET /v1/local-zip/" + kHashA] = FakeTransport::ok(R"({"error":{"code":"no_local_zip","message":"zipSource is BeatSaver"}})", 404);
  f.transport.responses["GET /v1/beatsaver?url=" + percentEncode(version.downloadUrl)] = FakeTransport::ok("REMOTE");
  CHECK(f.client.mapZip(kHashA, &version) == "REMOTE");

  // フォルダを使っているのにないときは、BeatSaver から取らない
  f.transport.responses["GET /v1/local-zip/" + kHashA] = FakeTransport::ok(R"({"error":{"code":"not_found","message":"the zip is not in the folder"}})", 404);
  auto before = f.transport.requests.size();
  std::string error;
  CHECK_FALSE(f.client.mapZip(kHashA, &version, &error));
  CHECK(error.find("zip folder") != std::string::npos);
  CHECK(f.transport.requests.size() == before + 1);
}
