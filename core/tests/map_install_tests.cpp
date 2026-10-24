#include "octantpath/core/map_install.hpp"
#include "octantpath/core/sha1.hpp"

#include <doctest/doctest.h>
#include <miniz/miniz.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <utility>
#include <vector>

using namespace octantpath::core;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path() / ("octantpath-install-" + std::to_string(std::random_device{}()));
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
};

std::string makeZip(std::vector<std::pair<std::string, std::string>> const &files) {
  mz_zip_archive archive{};
  REQUIRE(mz_zip_writer_init_heap(&archive, 0, 0));
  for (auto const &[name, data] : files) REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION));
  void *buffer = nullptr;
  size_t size = 0;
  REQUIRE(mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size));
  std::string zip(static_cast<char *>(buffer), size);
  mz_zip_writer_end(&archive);
  return zip;
}

std::string const kInfo = R"({"_version":"2.0.0","_difficultyBeatmapSets":[{"_beatmapCharacteristicName":"Standard","_difficultyBeatmaps":[{"_difficulty":"Expert","_beatmapFilename":"ExpertStandard.dat"}]}]})";
std::string const kChart = R"({"_notes":[]})";

std::string expectedHash() {
  Sha1 sha1;
  sha1.update(kInfo);
  sha1.update(kChart);
  return sha1.hex();
}

} // namespace

TEST_CASE("SHA-1") {
  Sha1 empty;
  CHECK(empty.hex() == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  Sha1 abc;
  abc.update("abc");
  CHECK(abc.hex() == "a9993e364706816aba3e25717850c26c9cd0d89d");
  // 2 度目も同じ値（取り出しで状態が進まない）
  CHECK(abc.hex() == "a9993e364706816aba3e25717850c26c9cd0d89d");
  Sha1 split;
  std::string longText(1000, 'a');
  split.update(longText.substr(0, 333));
  split.update(longText.substr(333));
  Sha1 whole;
  whole.update(longText);
  CHECK(split.hex() == whole.hex());
}

TEST_CASE("中身の名前はマップのフォルダの中だけ") {
  CHECK(safeEntryName("a/b.dat"));
  CHECK_FALSE(safeEntryName("../a"));
  CHECK_FALSE(safeEntryName("/a"));
  CHECK_FALSE(safeEntryName("a//b"));
  CHECK_FALSE(safeEntryName("C:a"));
  CHECK_FALSE(safeEntryName("a\\..\\b"));
}

TEST_CASE("フォルダ名は使えない文字を _ にし、80 文字まで") {
  CHECK(folderName("1a2b", "Song: A/B", "Me") == "1a2b (Song_ A_B - Me)");
  CHECK(folderName("k", std::string(100, 'x'), "m").size() == 80);
  CHECK(folderName("k", "和音", "m") == "k (和音 - m)");
  CHECK(safeFolderName("1a2b (Song - Me)"));
  CHECK_FALSE(safeFolderName("a/b"));
  CHECK_FALSE(safeFolderName(".."));
}

TEST_CASE("Info.dat と譜面を順に連結した SHA-1 で確かめてから置く") {
  TempDir dir;
  Settings settings;
  auto zip = makeZip({{"song/Info.dat", kInfo}, {"song/ExpertStandard.dat", kChart}, {"song/cover.jpg", "jpeg"}});
  auto result = installMap(expectedHash(), zip, "1a2b (Song - Me)", dir.path / "levels", dir.path / "work", settings);
  REQUIRE(result.ok());
  CHECK(result.hash == expectedHash());
  CHECK(fs::is_regular_file(dir.path / "levels" / "1a2b (Song - Me)" / "Info.dat"));
  CHECK(fs::is_regular_file(dir.path / "levels" / "1a2b (Song - Me)" / "cover.jpg"));
  CHECK(fs::is_empty(dir.path / "work"));
}

TEST_CASE("頼んだ版と hash が違えば置かない") {
  TempDir dir;
  auto zip = makeZip({{"Info.dat", kInfo}, {"ExpertStandard.dat", R"({"_notes":[1]})"}});
  auto result = installMap(expectedHash(), zip, "x", dir.path / "levels", dir.path / "work", Settings{});
  CHECK(result.failure == InstallFailure::HashMismatch);
  CHECK_FALSE(fs::exists(dir.path / "levels" / "x"));
  CHECK(fs::is_empty(dir.path / "work"));
}

TEST_CASE("外を指す名前、Info.dat の数、大きさ、同じ名前") {
  TempDir dir;
  Settings settings;
  CHECK(installMap(expectedHash(), makeZip({{"../Info.dat", kInfo}}), "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::BadEntryName);
  CHECK(installMap(expectedHash(), makeZip({{"a.dat", kChart}}), "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::NoInfo);
  CHECK(installMap(expectedHash(), makeZip({{"a/Info.dat", kInfo}, {"b/info.dat", kInfo}}), "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::ManyRoots);
  CHECK(installMap(expectedHash(), "not a zip", "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::ZipUnreadable);
  CHECK(installMap(expectedHash(), makeZip({{"Info.dat", R"({"_difficultyBeatmapSets":[{"_difficultyBeatmaps":[{"_beatmapFilename":"../x.dat"}]}]})"}}), "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::InfoPointsOutside);
  CHECK(installMap(expectedHash(), makeZip({{"Info.dat", kInfo}}), "x", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::InfoFileMissing);
  Settings tiny;
  tiny.zipMaxExtractedMegabytes = 0;
  CHECK(installMap(expectedHash(), makeZip({{"Info.dat", kInfo}, {"ExpertStandard.dat", kChart}}), "x", dir.path / "l", dir.path / "w", tiny).failure == InstallFailure::ExtractedTooLarge);
  fs::create_directories(dir.path / "l" / "taken");
  CHECK(installMap(expectedHash(), makeZip({{"Info.dat", kInfo}}), "taken", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::NameTaken);
  CHECK(installMap("nope", makeZip({{"Info.dat", kInfo}}), "y", dir.path / "l", dir.path / "w", settings).failure == InstallFailure::BadHash);
}

TEST_CASE("v4 の Info.dat は音源、続いて難易度ごとに譜面とライティング") {
  TempDir dir;
  std::string info = R"({"version":"4.0.0","audio":{"audioDataFilename":"a.dat"},"difficultyBeatmaps":[{"beatmapDataFilename":"b.dat","lightshowDataFilename":"l.dat"}]})";
  for (auto [name, data] : std::vector<std::pair<std::string, std::string>>{{"Info.dat", info}, {"a.dat", "A"}, {"b.dat", "B"}, {"l.dat", "L"}}) std::ofstream(dir.path / name, std::ios::binary) << data;
  Sha1 sha1;
  for (auto part : {info, std::string("A"), std::string("B"), std::string("L")}) sha1.update(part);
  auto result = computeMapHash(dir.path);
  CHECK(result.hash == sha1.hex());
  CHECK(result.files == std::vector<std::string>{"Info.dat", "a.dat", "b.dat", "l.dat"});
}
