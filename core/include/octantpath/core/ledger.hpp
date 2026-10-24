#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// OctantPath が入れたマップの所有の記録（Mods/octantpath/ledger.json）と、ゲームを起動した日（state.json）。
// 形は PC 版の OwnershipLedger・ImportState と同じ（OctantPath 法 3.1・3.2）
namespace octantpath::core {

namespace map_states {
inline constexpr char const *kTrial = "trial";
inline constexpr char const *kKept = "kept";
inline constexpr char const *kDeleted = "deleted";
inline constexpr char const *kDeletedByUser = "deleted_by_user";
} // namespace map_states

// どの枠で入ったか。Quest は一覧の行を押して入れるので、枠はいつも manual
struct FrameEntry {
  static constexpr char const *kManual = "manual";

  std::string frame = kManual;
  std::string requestId;
  // 推薦の要求で使った手札の id。空なら書かない
  std::string preset;
  std::string characteristic;
  std::string difficulty;
  // UTC の ISO 8601（例: 2026-10-21T09:00:00Z）
  std::string at;
};

struct OwnedMap {
  std::string hash;
  std::string mapKey;
  // 曲の置き場所（SongCore の CustomLevels）の中のフォルダ名
  std::string folder;
  std::string acquiredAt;
  std::string state = map_states::kTrial;
  std::vector<FrameEntry> frames;
  std::optional<std::string> changedAt;
  std::optional<std::string> keptFolder;
};

struct LedgerFile {
  int formatVersion = 1;
  std::vector<OwnedMap> maps;
};

class OwnershipLedger {
public:
  static constexpr char const *kFileName = "ledger.json";

  explicit OwnershipLedger(std::filesystem::path directory);

  // ファイルがなければ空の記録。読めない（壊れている、知らない形式）ときは nullopt。そのときは所有を推測しない
  std::optional<LedgerFile> load() const;
  // 書きかけのファイルを残さないよう、別のファイルに書いてから置き換える
  void save(LedgerFile const &ledger) const;
  // 同じ hash とフォルダの記録を置き換えて足す。記録が読めないときは書き換えずに false。mutex() を持って読み書きする
  bool add(OwnedMap const &map) const;

  // 記録を読んで直して書くあいだ持つ鍵（プロセスに 1 つ）。片付けと、入れた曲の記録が、互いの書き込みを消さないため
  static std::mutex &mutex();

private:
  std::filesystem::path path_;
};

// ゲームを起動した日（ローカルの日付、yyyy-MM-dd）。お試しの期限を起動日で数えるため
class LaunchDays {
public:
  static constexpr char const *kFileName = "state.json";

  explicit LaunchDays(std::filesystem::path directory);

  // 読めないときは nullopt
  std::optional<std::vector<std::string>> load() const;
  // 今日を足して、足したあとの一覧を返す。読めないときは書き換えずに nullopt
  std::optional<std::vector<std::string>> record(std::string const &today) const;

private:
  std::filesystem::path path_;
};

using UtcTime = std::chrono::sys_seconds;

// UTC の ISO 8601（秒まで、末尾 Z）
std::string formatUtc(UtcTime time);
// "YYYY-MM-DDTHH:MM:SS" で始まる UTC の時刻（小数の秒と末尾の Z は読み飛ばす）。読めなければ nullopt
std::optional<UtcTime> parseUtc(std::string const &text);
// その時刻の、この端末の地方時の日付（yyyy-MM-dd）
std::string localDate(UtcTime time);

// 書きかけのファイルを残さないよう、別のファイルに書いてから置き換える
void writeFileAtomically(std::filesystem::path const &path, std::string const &text);

} // namespace octantpath::core
