#include "octantpath/core/ledger.hpp"
#include "octantpath/core/map_install.hpp"
#include "octantpath/core/trials.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

using namespace octantpath::core;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path() / ("octantpath-trials-" + std::to_string(std::random_device{}()));
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
};

void write(fs::path const &path, std::string const &text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

UtcTime at(char const *text) { return *parseUtc(text); }

// 取った日の地方時の日付は、テストでは UTC の日付とする
std::string utcDate(UtcTime time) { return formatUtc(time).substr(0, 10); }

struct World {
  TempDir dir;
  JanitorPaths paths{dir.path / "OctantPath", dir.path / "CustomLevels", {dir.path / "Playlists", dir.path / "Missing"}};
  Settings settings;
  JanitorInputs inputs;

  World() {
    fs::create_directories(paths.playlists[0]);
    settings.deleteDryRun = false;
    inputs.launchDays = {"2026-10-20", "2026-10-21"};
  }

  // 中身の違うマップのフォルダを作り、その hash を記録に足す。chart の大きさで合計の大きさを決める
  OwnedMap addMap(std::string const &folder, char const *acquiredAt, size_t chartBytes = 10) {
    std::string info = R"({"_version":"2.0.0","_songName":")" + folder +
                       R"(","_difficultyBeatmapSets":[{"_beatmapCharacteristicName":"Standard","_difficultyBeatmaps":[{"_difficulty":"Expert","_beatmapFilename":"E.dat"}]}]})";
    write(paths.levels / folder / "Info.dat", info);
    write(paths.levels / folder / "E.dat", std::string(chartBytes, 'x'));
    auto hash = computeMapHash(paths.levels / folder);
    REQUIRE_FALSE(hash.hash.empty());
    OwnedMap map{hash.hash, "k" + folder, folder, acquiredAt};
    map.frames.push_back({FrameEntry::kManual, "request", "song", "Standard", "Expert", acquiredAt});
    REQUIRE(OwnershipLedger(paths.root).add(map));
    return map;
  }

  JanitorReport run() { return runJanitor(paths, settings, inputs, [] { return at("2026-10-21T12:00:00Z"); }); }

  std::string state(std::string const &hash) const {
    auto ledger = OwnershipLedger(paths.root).load();
    REQUIRE(ledger);
    for (auto const &map : ledger->maps) {
      if (map.hash == hash) return map.state;
    }
    return {};
  }
};

} // namespace

TEST_CASE("UTC の時刻は書いた形で読み戻せる") {
  auto time = at("2026-10-21T09:08:07Z");
  CHECK(formatUtc(time) == "2026-10-21T09:08:07Z");
  CHECK(formatUtc(*parseUtc("2026-10-21T09:08:07.1234567Z")) == "2026-10-21T09:08:07Z");
  CHECK(formatUtc(*parseUtc("1969-12-31T23:59:59Z")) == "1969-12-31T23:59:59Z");
  CHECK_FALSE(parseUtc("2026-10-21"));
  CHECK_FALSE(parseUtc("2026-13-01T00:00:00Z"));
  CHECK(formatUtc(*parseUtc("2024-02-29T12:00:00Z")) == "2024-02-29T12:00:00Z");
  CHECK(formatUtc(*parseUtc("2026-02-31T00:00:00Z")) == "2026-03-03T00:00:00Z");
}

TEST_CASE("所有の記録は PC 版と同じ形で書き、読めないときは推測しない") {
  TempDir dir;
  OwnershipLedger ledger(dir.path);
  REQUIRE(ledger.load());
  CHECK(ledger.load()->maps.empty());
  OwnedMap map{"0123456789abcdef0123456789abcdef01234567", "1a2b", "1a2b (Song - Me)", "2026-10-21T09:00:00Z"};
  map.frames.push_back({FrameEntry::kManual, "request", "", "Standard", "Expert", "2026-10-21T09:00:00Z"});
  REQUIRE(ledger.add(map));
  REQUIRE(ledger.add(map));
  auto loaded = ledger.load();
  REQUIRE(loaded);
  REQUIRE(loaded->maps.size() == 1);
  CHECK(loaded->maps[0].state == "trial");
  CHECK(loaded->maps[0].frames[0].frame == "manual");

  std::ifstream file(dir.path / "ledger.json");
  auto text = std::string(std::istreambuf_iterator<char>(file), {});
  CHECK(text.find("\"formatVersion\": 1") != std::string::npos);
  CHECK(text.find("\"preset\"") == std::string::npos);

  for (auto broken : {"{", R"({"formatVersion": 2, "maps": []})", R"({"maps": []})", R"({"formatVersion": 1, "maps": [{"hash": "a"}]})"}) {
    write(dir.path / "ledger.json", broken);
    CHECK_FALSE(ledger.load());
    CHECK_FALSE(ledger.add(map));
  }
}

TEST_CASE("起動日は 1 日 1 回だけ足す") {
  TempDir dir;
  LaunchDays days(dir.path);
  CHECK(days.record("2026-10-20")->size() == 1);
  CHECK(days.record("2026-10-20")->size() == 1);
  CHECK(days.record("2026-10-21")->size() == 2);
  write(dir.path / "state.json", "{");
  CHECK_FALSE(days.record("2026-10-22"));
}

TEST_CASE("お試しの判定は、お気に入り、遊んだ回数、起動日の期限の順") {
  OwnedMap map{"h", "k", "f", "2026-10-10T00:00:00Z"};
  std::vector<std::string> days = {"2026-10-09", "2026-10-10", "2026-10-11", "2026-10-12"};
  CHECK(TrialPolicy({"h"}, {{"h", 0}}, days, 3, 1, utcDate).decide(map).decision == TrialDecision::Keep);
  CHECK(TrialPolicy({}, {{"h", 3}}, days, 3, 1, utcDate).decide(map).decision == TrialDecision::Keep);
  CHECK(TrialPolicy({}, {{"h", 2}}, days, 3, 2, utcDate).decide(map).decision == TrialDecision::Delete);
  CHECK(TrialPolicy({}, {{"h", 2}}, days, 3, 3, utcDate).decide(map).decision == TrialDecision::Hold);
  CHECK(TrialPolicy({}, {}, days, 3, 3, utcDate).launchDaysSince(at("2026-10-10T00:00:00Z")) == 3);
  map.state = "kept";
  CHECK(TrialPolicy({"h"}, {}, days, 3, 1, utcDate).decide(map).decision == TrialDecision::Hold);
}

TEST_CASE("合計が上限を超えたら、古く取ったものから消し、記録を消したことにする") {
  World world;
  world.settings.trialMaxMegabytes = 1.5;
  auto oldest = world.addMap("old", "2026-10-20T00:00:00Z", 1024 * 1024);
  auto newer = world.addMap("new", "2026-10-21T00:00:00Z", 1024 * 1024);
  auto report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK(report.levelsChanged);
  CHECK_FALSE(fs::exists(world.paths.levels / "old"));
  CHECK(fs::exists(world.paths.levels / "new"));
  CHECK(world.state(oldest.hash) == "deleted");
  CHECK(world.state(newer.hash) == "trial");
  CHECK(fs::is_empty(world.paths.root / "Deleting"));
}

TEST_CASE("お気に入りと遊んだマップは残し、プレイリストに入ったものは消さない") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  auto liked = world.addMap("liked", "2026-10-20T00:00:00Z");
  auto played = world.addMap("played", "2026-10-20T00:00:00Z");
  auto listed = world.addMap("listed", "2026-10-20T00:00:00Z");
  auto expired = world.addMap("expired", "2026-10-20T00:00:00Z");
  world.inputs.liked = {liked.hash};
  world.inputs.plays = {{played.hash, 3}};
  write(world.paths.playlists[0] / "mine.bplist", R"({"songs": [{"hash": ")" + listed.hash + R"("}]})");
  write(world.paths.playlists[0] / "ours.bplist", R"({"customData": {"OctantPath": true}, "songs": [{"levelid": "custom_level_)" + expired.hash + R"("}]})");
  auto report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK(world.state(liked.hash) == "kept");
  CHECK(world.state(played.hash) == "kept");
  CHECK(world.state(listed.hash) == "trial");
  CHECK(world.state(expired.hash) == "deleted");
  CHECK(fs::exists(world.paths.levels / "liked"));
  CHECK(fs::exists(world.paths.levels / "listed"));
  CHECK_FALSE(fs::exists(world.paths.levels / "expired"));
}

TEST_CASE("中身が記録と違うフォルダは消さない") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  auto map = world.addMap("changed", "2026-10-20T00:00:00Z");
  write(world.paths.levels / "changed" / "E.dat", "replaced by the user");
  auto report = world.run();
  CHECK(fs::exists(world.paths.levels / "changed"));
  CHECK(world.state(map.hash) == "trial");
  REQUIRE(report.lines.size() == 1);
  CHECK(report.lines[0].find("中身が記録と違う") != std::string::npos);
}

TEST_CASE("試し運転では何も動かさない") {
  World world;
  world.settings.deleteDryRun = true;
  world.settings.trialExpiryLaunchDays = 1;
  auto map = world.addMap("expired", "2026-10-20T00:00:00Z");
  auto report = world.run();
  CHECK(report.dryRun);
  CHECK_FALSE(report.levelsChanged);
  CHECK(fs::exists(world.paths.levels / "expired"));
  CHECK(world.state(map.hash) == "trial");
  REQUIRE(report.lines.size() == 1);
  CHECK(report.lines[0].starts_with("消す（試し運転）"));
}

TEST_CASE("守る情報が読めないときは、まとめて止める") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  world.addMap("expired", "2026-10-20T00:00:00Z");

  world.settings.unreadable = "broken";
  CHECK(world.run().stopped);
  world.settings.unreadable.clear();

  world.inputs.unreadable = "ゲームの記録が読めない";
  CHECK(world.run().stopped);
  world.inputs.unreadable.clear();

  write(world.paths.playlists[0] / "broken.json", "{");
  CHECK(world.run().stopped);
  fs::remove(world.paths.playlists[0] / "broken.json");

  // プレイリストの置き場所がひとつもないときも止める
  fs::rename(world.paths.playlists[0], world.dir.path / "Playlists.away");
  auto noPlaylists = world.run();
  CHECK(noPlaylists.stopped);
  CHECK(noPlaylists.stopReason.find("プレイリストの置き場所") != std::string::npos);
  fs::rename(world.dir.path / "Playlists.away", world.paths.playlists[0]);

  auto ledger = world.paths.root / "ledger.json";
  fs::copy_file(ledger, world.dir.path / "ledger.json.bak");
  write(ledger, "{");
  CHECK(world.run().stopped);
  fs::copy_file(world.dir.path / "ledger.json.bak", ledger, fs::copy_options::overwrite_existing);

  CHECK(fs::exists(world.paths.levels / "expired"));
  auto report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK_FALSE(fs::exists(world.paths.levels / "expired"));
}

TEST_CASE("選曲かプレイが始まったら、残りは消さずに止め、決めた「残す」は書く") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  auto liked = world.addMap("liked", "2026-10-19T00:00:00Z");
  auto first = world.addMap("first", "2026-10-20T00:00:00Z");
  auto second = world.addMap("second", "2026-10-20T01:00:00Z");
  world.inputs.liked = {liked.hash};
  int asked = 0;
  auto report = runJanitor(world.paths, world.settings, world.inputs, [] { return at("2026-10-21T12:00:00Z"); }, [&] { return ++asked > 1; });
  CHECK(report.stopped);
  CHECK(report.levelsChanged);
  CHECK(world.state(liked.hash) == "kept");
  CHECK(world.state(first.hash) == "deleted");
  CHECK(world.state(second.hash) == "trial");
  CHECK(fs::exists(world.paths.levels / "second"));
}

TEST_CASE("片付けのあいだ、所有の記録の鍵を持つ") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  auto map = world.addMap("held", "2026-10-20T00:00:00Z");
  bool heldDuringRun = false;
  runJanitor(world.paths, world.settings, world.inputs, [&] {
    // 片付けが記録を書くあいだに、ほかの書き手（入れた曲の記録）は入れない（now は消すときに呼ばれる）
    heldDuringRun = !OwnershipLedger::mutex().try_lock();
    if (!heldDuringRun) OwnershipLedger::mutex().unlock();
    return at("2026-10-21T12:00:00Z");
  });
  CHECK(heldDuringRun);
  CHECK(world.state(map.hash) == "deleted");
  // 終われば鍵は外れている
  CHECK(OwnershipLedger::mutex().try_lock());
  OwnershipLedger::mutex().unlock();
}

TEST_CASE("期限・回数・上限が 0 以下や有限でないときは、何も消さない") {
  for (int i = 0; i < 6; ++i) {
    World world;
    world.settings.trialExpiryLaunchDays = 1;
    auto map = world.addMap("expired", "2026-10-20T00:00:00Z");
    switch (i) {
    case 0: world.settings.trialExpiryLaunchDays = 0; break;
    case 1: world.settings.trialExpiryLaunchDays = -1; break;
    case 2: world.settings.keepMinPlays = 0; break;
    case 3: world.settings.trialMaxMegabytes = 0; break;
    case 4: world.settings.trialMaxMegabytes = -1; break;
    default: world.settings.trialMaxMegabytes = std::numeric_limits<double>::infinity(); break;
    }
    auto report = world.run();
    CHECK(report.stopped);
    CHECK(fs::exists(world.paths.levels / "expired"));
    CHECK(world.state(map.hash) == "trial");
  }
}

TEST_CASE("とても大きい上限でも、全部が上限を超えたことにはならない") {
  World world;
  world.settings.trialMaxMegabytes = 1e300;
  auto map = world.addMap("new", "2026-10-21T00:00:00Z");
  auto report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK(fs::exists(world.paths.levels / "new"));
  CHECK(world.state(map.hash) == "trial");
}

TEST_CASE("利用者が消したお試しは記録を変え、同じマップを入れ直しても消さない") {
  World world;
  world.settings.trialExpiryLaunchDays = 1;
  auto map = world.addMap("again", "2026-10-20T00:00:00Z");
  auto copy = world.dir.path / "copy";
  fs::copy(world.paths.levels / "again", copy, fs::copy_options::recursive);
  fs::remove_all(world.paths.levels / "again");
  auto report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK(world.state(map.hash) == "deleted_by_user");

  // 利用者が同じマップを同じフォルダ名で入れ直す
  fs::copy(copy, world.paths.levels / "again", fs::copy_options::recursive);
  report = world.run();
  CHECK_FALSE(report.stopped);
  CHECK(fs::exists(world.paths.levels / "again"));
  CHECK(world.state(map.hash) == "deleted_by_user");
}
