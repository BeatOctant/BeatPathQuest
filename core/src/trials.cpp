#include "octantpath/core/trials.hpp"

#include "octantpath/core/beatsaver.hpp"
#include "octantpath/core/map_install.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace octantpath::core {

namespace {

constexpr char const *kDeletingFolderName = "Deleting";
constexpr char const *kPlaylistMark = "OctantPath";
constexpr char const *kCustomLevelPrefix = "custom_level_";

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// フォルダの中のファイルの合計（バイト）。読めなければ 0
std::uintmax_t folderBytes(std::filesystem::path const &folder) {
  std::uintmax_t total = 0;
  std::error_code error;
  if (!std::filesystem::is_directory(folder, error)) return 0;
  for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
    std::error_code size;
    if (it->is_regular_file(size)) total += it->file_size(size);
  }
  return error ? 0 : total;
}

std::string randomSuffix() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device device;
  std::string suffix;
  for (int i = 0; i < 16; ++i) suffix += kHex[device() % 16];
  return suffix;
}

// 消す・残す直前に、置き場所の中にあり、中身の hash が記録と同じか確かめる（OctantPath 法 3.3）。問題がなければ空
std::string checkFolder(OwnedMap const &map, std::filesystem::path const &levels) {
  if (!safeFolderName(map.folder)) return "記録のフォルダ名が使えない";
  auto folder = levels / map.folder;
  std::error_code error;
  if (!std::filesystem::is_directory(folder, error)) return "フォルダがない";
  auto hash = computeMapHash(folder);
  if (hash.hash.empty() || hash.hash != map.hash) return "中身が記録と違う";
  return {};
}

std::string line(OwnedMap const &map, std::string const &action, std::string const &reason) { return action + ": " + map.mapKey + " " + map.hash + " " + map.folder + "（" + reason + "）"; }

} // namespace

TrialPolicy::TrialPolicy(std::set<std::string> liked, std::map<std::string, int> plays, std::vector<std::string> launchDays, int keepMinPlays, int expiryLaunchDays,
                         std::function<std::string(UtcTime)> toLocalDate)
    : liked_(std::move(liked)), plays_(std::move(plays)), launchDays_(std::move(launchDays)), keepMinPlays_(keepMinPlays), expiryLaunchDays_(expiryLaunchDays),
      toLocalDate_(std::move(toLocalDate)) {
  std::sort(launchDays_.begin(), launchDays_.end());
  launchDays_.erase(std::unique(launchDays_.begin(), launchDays_.end()), launchDays_.end());
}

TrialVerdict TrialPolicy::decide(OwnedMap const &map) const {
  if (map.state != map_states::kTrial) return {TrialDecision::Hold, "お試し中ではない"};
  // 好意の印があるマップは消さない（OctantPath 法 3.5）
  if (liked_.contains(map.hash)) return {TrialDecision::Keep, "お気に入り"};
  auto played = plays_.find(map.hash);
  int plays = played == plays_.end() ? 0 : played->second;
  if (plays >= keepMinPlays_) return {TrialDecision::Keep, std::to_string(plays) + " 回遊んだ"};
  auto acquired = parseUtc(map.acquiredAt);
  if (!acquired) return {TrialDecision::Hold, "取った日時が読めない"};
  auto days = launchDaysSince(*acquired);
  if (days > expiryLaunchDays_) return {TrialDecision::Delete, "お試しの期限（" + std::to_string(expiryLaunchDays_) + " 起動日）を過ぎた"};
  return {TrialDecision::Hold, std::to_string(days) + " 起動日目、" + std::to_string(plays) + " 回"};
}

int TrialPolicy::launchDaysSince(UtcTime acquiredAt) const {
  auto acquired = toLocalDate_(acquiredAt);
  // yyyy-MM-dd は文字列の順が日付の順
  return static_cast<int>(std::count_if(launchDays_.begin(), launchDays_.end(), [&](std::string const &day) { return day.size() == 10 && day >= acquired; }));
}

std::optional<std::set<std::string>> playlistHashes(std::filesystem::path const &directory, std::string &error) {
  std::set<std::string> hashes;
  std::error_code exists;
  if (!std::filesystem::is_directory(directory, exists)) return hashes;
  std::error_code walk;
  for (std::filesystem::recursive_directory_iterator it(directory, walk), end; it != end; it.increment(walk)) {
    if (walk) {
      error = "プレイリストの置き場所が読めない: " + walk.message();
      return std::nullopt;
    }
    auto extension = lower(it->path().extension().string());
    std::error_code type;
    if (!it->is_regular_file(type) || (extension != ".bplist" && extension != ".json")) continue;
    json playlist;
    try {
      std::ifstream file(it->path());
      std::stringstream text;
      text << file.rdbuf();
      playlist = json::parse(text.str());
    } catch (std::exception const &e) {
      error = it->path().filename().string() + " が読めない: " + e.what();
      return std::nullopt;
    }
    if (!playlist.is_object()) continue;
    if (auto data = playlist.find("customData"); data != playlist.end() && data->is_object() && data->contains(kPlaylistMark)) continue;
    auto songs = playlist.find("songs");
    if (songs == playlist.end() || !songs->is_array()) continue;
    for (auto const &song : *songs) {
      if (!song.is_object()) continue;
      std::optional<std::string> hash;
      if (auto it = song.find("hash"); it != song.end() && it->is_string()) hash = normalizeHash(it->get<std::string>());
      for (auto key : {"levelid", "levelId"}) {
        if (hash) break;
        auto id = song.find(key);
        if (id == song.end() || !id->is_string()) continue;
        auto text = id->get<std::string>();
        if (lower(text).starts_with(kCustomLevelPrefix)) hash = normalizeHash(text.substr(std::string(kCustomLevelPrefix).size(), 40));
      }
      if (hash) hashes.insert(*hash);
    }
  }
  if (walk) {
    error = "プレイリストの置き場所が読めない: " + walk.message();
    return std::nullopt;
  }
  return hashes;
}

JanitorReport runJanitor(JanitorPaths const &paths, Settings const &settings, JanitorInputs const &inputs, std::function<UtcTime()> now, std::function<bool()> interrupted) {
  JanitorReport report;
  report.dryRun = settings.deleteDryRun;
  auto stop = [&](std::string reason) {
    report.stopped = true;
    report.stopReason = std::move(reason);
    return report;
  };

  // 守る情報が読めないときは、まとめて止める（OctantPath 法 3.2・3.4）
  if (!settings.unreadable.empty()) return stop("設定（settings.json）が読めないので、何も消さない");
  if (!inputs.unreadable.empty()) return stop(inputs.unreadable + "ので、何も消さない");
  if (auto problem = settings.deletionProblem(); !problem.empty()) return stop(problem);
  OwnershipLedger store(paths.root);
  std::lock_guard lock(OwnershipLedger::mutex());
  auto ledger = store.load();
  if (!ledger) return stop("所有の記録（ledger.json）が読めないので、何も消さない");
  // プレイリストの置き場所はひとつも見つからなければ止める。場所の見当違いで、プレイリストに入れた曲を守れないまま消さないため
  if (!paths.playlists.empty() && std::none_of(paths.playlists.begin(), paths.playlists.end(), [](auto const &directory) {
        std::error_code error;
        return std::filesystem::is_directory(directory, error);
      }))
    return stop("プレイリストの置き場所が見つからないので、何も消さない");
  std::set<std::string> protectedSet;
  for (auto const &directory : paths.playlists) {
    std::string playlistError;
    auto hashes = playlistHashes(directory, playlistError);
    if (!hashes) return stop("プレイリストが読めないので、何も消さない: " + playlistError);
    protectedSet.insert(hashes->begin(), hashes->end());
  }
  auto protectedHashes = &protectedSet;

  bool changed = false;
  // 利用者がゲームの中などで消したお試しは、記録を deleted_by_user にして以後は扱わない（PC 版の CollectUserDeletes と同じ）。
  // Quest はお試しも利用者の曲も同じ CustomLevels に置くので、trial のまま残すと、あとで利用者が同じマップを同じ名前で入れ直したときに消してしまう
  std::error_code levelsExists;
  if (std::filesystem::is_directory(paths.levels, levelsExists)) {
    for (auto &map : ledger->maps) {
      if (map.state != map_states::kTrial || !safeFolderName(map.folder)) continue;
      std::error_code exists;
      if (std::filesystem::exists(paths.levels / map.folder, exists) || exists) continue;
      map.state = map_states::kDeletedByUser;
      map.changedAt = formatUtc(now());
      changed = true;
      report.lines.push_back(line(map, "記録だけ変えた", "フォルダがないので、利用者が消したとみなす"));
    }
  }

  TrialPolicy policy(inputs.liked, inputs.plays, inputs.launchDays, settings.keepMinPlays, settings.trialExpiryLaunchDays);
  std::vector<std::pair<OwnedMap *, TrialVerdict>> decisions;
  for (auto &map : ledger->maps) {
    if (map.state == map_states::kTrial) decisions.emplace_back(&map, policy.decide(map));
  }
  std::stable_sort(decisions.begin(), decisions.end(), [](auto const &a, auto const &b) { return a.first->acquiredAt < b.first->acquiredAt; });

  // 期限の日数のほかに、お試しの合計の大きさでも消す（どちらかを超えたら。2026-10-20）。
  // 残るものの合計が上限を超えるあいだ、古く取ったものから消す。ほかのプレイリストに入っているものは消さない
  std::uintmax_t total = 0;
  for (auto const &[map, verdict] : decisions) {
    if (verdict.decision == TrialDecision::Hold) total += folderBytes(paths.levels / map->folder);
  }
  // uintmax_t に入らない値は最大値にそろえる
  auto maxMegabytesBytes = settings.trialMaxMegabytes * 1024 * 1024;
  auto maxBytes = maxMegabytesBytes >= static_cast<double>(std::numeric_limits<std::uintmax_t>::max()) ? std::numeric_limits<std::uintmax_t>::max()
                                                                                                        : static_cast<std::uintmax_t>(maxMegabytesBytes);
  std::set<OwnedMap const *> overCapacity;
  for (auto const &[map, verdict] : decisions) {
    if (total <= maxBytes) break;
    if (verdict.decision != TrialDecision::Hold || protectedHashes->contains(map->hash)) continue;
    overCapacity.insert(map);
    total -= std::min(total, folderBytes(paths.levels / map->folder));
  }

  // 途中で止めても、それまでに決めた「残す」は書く
  bool paused = false;
  for (auto &[map, result] : decisions) {
    auto verdict = overCapacity.contains(map) ? TrialVerdict{TrialDecision::Delete, "お試しの合計が上限（" + std::to_string(static_cast<long long>(settings.trialMaxMegabytes)) + " MB）を超えた"}
                                              : result;
    if (verdict.decision == TrialDecision::Hold) continue;
    if (verdict.decision == TrialDecision::Delete && protectedHashes->contains(map->hash)) {
      report.lines.push_back(line(*map, "残す", "ほかのプレイリストに入っている"));
      continue;
    }
    if (auto problem = checkFolder(*map, paths.levels); !problem.empty()) {
      report.lines.push_back(line(*map, "そのまま", problem));
      continue;
    }

    if (verdict.decision == TrialDecision::Keep) {
      if (settings.deleteDryRun) {
        report.lines.push_back(line(*map, "残す（試し運転）", verdict.reason));
        continue;
      }
      // Quest では曲はもとから CustomLevels にあるので、記録だけを変える
      map->state = map_states::kKept;
      map->keptFolder = map->folder;
      map->changedAt = formatUtc(now());
      changed = true;
      report.lines.push_back(line(*map, "残した", verdict.reason));
      continue;
    }

    if (settings.deleteDryRun) {
      report.lines.push_back(line(*map, "消す（試し運転）", verdict.reason));
      continue;
    }
    if (interrupted && interrupted()) {
      paused = true;
      stop("選曲かプレイが始まったので、残りは次の起動で決める");
      break;
    }
    // 戻せる写しを残すため、まず同じファイルシステムの中で脇へ移してから消す（OctantPath 法 3.6）
    auto folder = paths.levels / map->folder;
    auto aside = paths.root / kDeletingFolderName / (map->folder + "." + randomSuffix());
    std::error_code error;
    std::filesystem::create_directories(aside.parent_path(), error);
    if (!error) std::filesystem::rename(folder, aside, error);
    if (error) {
      report.lines.push_back(line(*map, "そのまま", "脇へ移せない: " + error.message()));
      stop("移せない食い違いが起きたので、残りは消さない");
      break;
    }
    report.levelsChanged = true;
    map->state = map_states::kDeleted;
    map->changedAt = formatUtc(now());
    try {
      store.save(*ledger);
    } catch (std::exception const &e) {
      // 記録できないなら元に戻して止める
      std::error_code back;
      std::filesystem::rename(aside, folder, back);
      map->state = map_states::kTrial;
      map->changedAt.reset();
      stop(std::string("所有の記録を書けないので止める: ") + e.what() + (back ? "（" + map->folder + " を戻せず、" + aside.string() + " にある）" : ""));
      break;
    }
    changed = false;
    std::error_code removed;
    std::filesystem::remove_all(aside, removed);
    report.lines.push_back(line(*map, "消した", verdict.reason));
  }

  if (changed && (!report.stopped || paused)) store.save(*ledger);
  return report;
}

} // namespace octantpath::core
