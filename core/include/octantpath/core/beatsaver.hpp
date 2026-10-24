#pragma once

#include "octantpath/core/recommendations.hpp"
#include "octantpath/core/settings.hpp"
#include "octantpath/core/transport.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace octantpath::core {

// BeatSaver API のマップ（外部依存。使う項目だけ。PC 版の BeatSaverModels と同じ）
struct BeatSaverDiff {
  std::string characteristic;
  std::string difficulty;
  std::optional<double> nps;
  std::optional<double> njs;
};

struct BeatSaverVersion {
  std::string hash;
  std::string state;
  std::string downloadUrl;
  std::string coverUrl;
  std::string previewUrl;
  std::vector<BeatSaverDiff> diffs;
};

struct BeatSaverMetadata {
  std::string songName;
  std::string songSubName;
  std::string songAuthorName;
  std::string levelAuthorName;
  double duration = 0;
};

struct BeatSaverMap {
  std::string key;
  std::string name;
  BeatSaverMetadata metadata;
  std::vector<BeatSaverVersion> versions;

  // 頼んだ hash の版だけを返す。先頭の版や別の版で代用しない（OctantPath 法 2.1）
  BeatSaverVersion const *version(std::string const &hash) const;
};

// 40 桁の 16 進なら小文字にして返す。違う形なら nullopt
std::optional<std::string> normalizeHash(std::string const &hash);

// https で、ホストが beatsaver.com かその下だけ（PC 版の BeatSaverUrls と同じ）
bool allowedBeatSaverUrl(std::string const &url);

BeatSaverMap parseBeatSaverMap(std::string const &text);
// /maps/hash/{h1,h2,...} の応答。1 件だけ頼んだときはマップがそのまま返る。キーは小文字の hash
std::map<std::string, BeatSaverMap> parseMapsByHash(std::string const &text, std::vector<std::string> const &asked);

std::string percentEncode(std::string const &text);

// BeatSaver API を、PC のプロキシ（OctantPathAndroid の proxy/）越しに引く。問い合わせはまとめて（1 回 50 件まで）、直列に、間隔を空けて送り、
// 429 には間隔を倍にして再試行し、上限で止める（PC 版の BeatSaverClient と同じ。プロキシも直列と間隔を守る）
class BeatSaverClient {
public:
  static constexpr size_t kMaxHashesPerRequest = 50;
  // 設定の間隔と最大の待ちの下限
  static constexpr std::chrono::milliseconds kMinApiInterval{1000};

  using Sleep = std::function<void(std::chrono::milliseconds)>;
  BeatSaverClient(Transport &transport, std::function<Settings()> settings, Sleep sleep = nullptr);

  struct Lookup {
    // 見つかったマップ。見つからなかった hash は入らない
    std::map<std::string, BeatSaverMap> maps;
    // 引けなかった理由（プロキシに届かない、BeatSaver が失敗を返したなど）。全部引けたら空
    std::string error;
  };

  Lookup mapsByHash(std::vector<std::string> const &hashes);

  // CDN のファイル（カバー・試聴・zip）をプロキシ越しに取る。BeatSaver の URL でなければ取らない。
  // 取れなければ nullopt で、理由は error に入れる。同時に取る数と間隔はプロキシが守る
  std::optional<std::string> download(std::string const &url, std::string *error = nullptr);

  // 推薦された版の zip を取る。PC のプロキシに zip のフォルダ（zipSource）があればそこから、なければ BeatSaver の版の downloadURL から。
  // フォルダを使っているのにそこにない zip は、BeatSaver から取らない（OctantPath 法 2.8）
  std::optional<std::string> mapZip(std::string const &hash, BeatSaverVersion const *version, std::string *error = nullptr);

private:
  // 本文。見つからなければ nullopt。失敗は例外（std::runtime_error）
  std::optional<std::string> get(std::string const &path);

  Transport &transport_;
  std::function<Settings()> settings_;
  Sleep sleep_;
  std::mutex serial_;
  std::chrono::steady_clock::time_point lastRequest_{};
};

// 推薦の 1 譜面と、表示のために BeatSaver から引いた情報（PC 版の RecommendationItem と同じ意味）
struct RecommendationItem {
  Candidate candidate;
  // 小文字にそろえた hash。OctantCompass が形の違う hash を返したら空
  std::string hash;
  std::optional<BeatSaverMap> map;
  // 推薦された hash の版だけ。なければ画面に出さない（2026-10-11。削除されたマップと古い版は、試聴もカバーも取得もできない）
  std::optional<BeatSaverVersion> version;
  std::optional<double> nps;
  std::optional<double> njs;

  bool shown() const { return version.has_value(); }
};

// 候補に BeatSaver の情報を付ける。順は OctantCompass が返した順のまま（憲法の原則 6）。候補は全部返す
std::vector<RecommendationItem> recommendationItems(RecommendationResponse const &response, BeatSaverClient::Lookup const &lookup);

} // namespace octantpath::core
