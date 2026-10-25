#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace octantpath::core {

// OctantPath の設定（settings.json）。いまは接続と推薦に使う分だけ。知らない項目は読み飛ばす
struct Settings {
  int hostRequestTimeoutSeconds = 10;
  int healthRetryIntervalSeconds = 10;
  int recommendationRequestTimeoutSeconds = 30;
  // 推薦を何曲ずつ見せるか。一度に頼むのはこの 3 倍（PC 版の最初の要求と同じ）
  int mixDraftCount = 5;
  // BeatSaver API。Quest はネットにつながないので、PC のプロキシ（OctantPathAndroid の proxy/）越しに引く。
  // プロキシには USB の adb reverse で届く（既定は 127.0.0.1:23288）
  std::string beatSaverApiUrl = "https://api.beatsaver.com/";
  double beatSaverApiIntervalSeconds = 1;
  int beatSaverApiMaxBackoffSeconds = 60;
  std::string beatSaverProxyHost = "127.0.0.1";
  int beatSaverProxyPort = 23288;
  // CDN（カバー・試聴・zip）。待ち時間と、1 つの応答の大きさの上限（プロキシも同じ上限を守る）
  int cdnTimeoutSeconds = 30;
  int zipMaxDownloadMegabytes = 100;
  // zip を展開するときの上限（中身の数と、展開した大きさ）
  int zipMaxEntries = 2000;
  int zipMaxExtractedMegabytes = 500;
  // ペアリングのときに OctantCompass に見せる名前。空なら MOD が決めた既定の名前
  std::string environmentName;
  // 画面の言語（"auto"、"ja"、"en"）
  std::string language = "auto";
  // お試し中のマップ（OctantPath が入れて、まだ残すと決まっていないもの）。名前と既定値は PC 版と同じ。
  // 遊んだ回数（クリアと失敗を合わせた、ゲームの記録の回数）がこの回数以上なら残す
  int keepMinPlays = 3;
  // お試しの期限（起動日）。ゲームを起動した日を 1 日と数え、この日数を過ぎてもお試し中なら消す
  int trialExpiryLaunchDays = 15;
  // お試し中のマップの合計の上限（MB）。期限の日数とどちらかを超えたら、古く取ったものから消す
  double trialMaxMegabytes = 10240;
  // 削除の試し運転。消す代わりにログに書くだけ
  bool deleteDryRun = true;

  // settings.json が読めなかったときの理由（既定値で動くが、削除は止める）
  std::string unreadable;

  // お試しの片付けを止める理由。なければ空。
  // 0 や負の期限・回数・上限（「0 = 無制限」のつもりで書かれやすい）では、お試しを全部消す側に倒れるので何もしない
  std::string deletionProblem() const;

  std::chrono::milliseconds hostRequestTimeout() const { return std::chrono::seconds(hostRequestTimeoutSeconds); }
  std::chrono::milliseconds healthRetryInterval() const { return std::chrono::seconds(healthRetryIntervalSeconds); }
  std::chrono::milliseconds recommendationRequestTimeout() const { return std::chrono::seconds(recommendationRequestTimeoutSeconds); }
};

// ファイルがなければ既定値。読めないときも既定値で動き、ファイルは書き換えない（warn に理由を渡す）
Settings loadSettings(std::filesystem::path const &directory, std::function<void(std::string const &)> const &warn);

} // namespace octantpath::core
