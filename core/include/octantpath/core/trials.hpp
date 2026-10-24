#pragma once

#include "octantpath/core/ledger.hpp"
#include "octantpath/core/settings.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// お試し中のマップを残すか消すか（OctantPath 法 3.3〜3.6。PC 版の TrialPolicy・PlaylistGuard・Janitor と同じ決まり）。
// Quest では入れた曲は SongCore の CustomLevels に置くので、「残す」は記録を kept にするだけで、フォルダは動かさない
namespace octantpath::core {

enum class TrialDecision { Hold, Keep, Delete };

struct TrialVerdict {
  TrialDecision decision = TrialDecision::Hold;
  std::string reason;
};

class TrialPolicy {
public:
  // liked: お気に入りの hash。plays: hash ごとの遊んだ回数（Quest ではゲームの記録の、そのマップの全難易度の回数の合計）。
  // launchDays: ゲームを起動した日（yyyy-MM-dd）。toLocalDate: 取った時刻を地方時の日付にする
  TrialPolicy(std::set<std::string> liked, std::map<std::string, int> plays, std::vector<std::string> launchDays, int keepMinPlays, int expiryLaunchDays,
              std::function<std::string(UtcTime)> toLocalDate = localDate);

  TrialVerdict decide(OwnedMap const &map) const;
  // 取った日も含めて、ゲームを起動した日を数える
  int launchDaysSince(UtcTime acquiredAt) const;

private:
  std::set<std::string> liked_;
  std::map<std::string, int> plays_;
  std::vector<std::string> launchDays_;
  int keepMinPlays_;
  int expiryLaunchDays_;
  std::function<std::string(UtcTime)> toLocalDate_;
};

// ほかのプレイリストに入っているマップの hash（小文字）。OctantPath が作ったプレイリスト（customData の OctantPath）は数えない。
// フォルダがなければ空。読めないプレイリストがあれば nullopt で、error に理由（守るものを見落とさないよう、削除を止める）
std::optional<std::set<std::string>> playlistHashes(std::filesystem::path const &directory, std::string &error);

struct JanitorPaths {
  // Mods/octantpath（記録と、消す前に脇へ移す Deleting）
  std::filesystem::path root;
  // 曲の置き場所（SongCore の CustomLevels）。root と同じファイルシステムに置く
  std::filesystem::path levels;
  // プレイリストの置き場所（ないものは飛ばす）
  std::vector<std::filesystem::path> playlists;
};

// 守る情報。ゲームの記録から MOD が集める。読めなかったら unreadable に理由を入れる（削除をまとめて止める）
struct JanitorInputs {
  std::set<std::string> liked;
  std::map<std::string, int> plays;
  std::vector<std::string> launchDays;
  std::string unreadable;
};

struct JanitorReport {
  bool dryRun = false;
  bool stopped = false;
  std::string stopReason;
  // 曲の置き場所の中身を変えたか（変えたら SongCore に読み直させる）
  bool levelsChanged = false;
  std::vector<std::string> lines;
};

// お試し中のマップを決まりに沿って残すか消す。期限の日数のほかに、お試しの合計の大きさでも消す（どちらかを超えたら、古く取ったものから）。
// 所有の記録の鍵（OwnershipLedger::mutex()）を、終わるまで持つ。
// interrupted は 1 件を脇へ移す直前に呼び、真なら残りは消さずに止める（選曲やプレイが始まったとき。OctantPath 法 2.6）
JanitorReport runJanitor(
    JanitorPaths const &paths, Settings const &settings, JanitorInputs const &inputs,
    std::function<UtcTime()> now = [] { return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()); },
    std::function<bool()> interrupted = nullptr);

} // namespace octantpath::core
