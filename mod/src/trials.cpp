#include "trials.hpp"

#include "game.hpp"
#include "main.hpp"
#include "services.hpp"

#include "octantpath/core/beatsaver.hpp"
#include "octantpath/core/trials.hpp"

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "songcore/shared/SongCore.hpp"

#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerLevelStatsData.hpp"
#include "System/Collections/Generic/Dictionary_2.hpp"
#include "System/Collections/Generic/HashSet_1.hpp"
#include "System/Collections/Generic/KeyValuePair_2.hpp"
#include "UnityEngine/Resources.hpp"

#include <chrono>
#include <thread>

namespace octantpath {

namespace {

constexpr char const *kCustomLevelPrefix = "custom_level_";

// ゲームの譜面の id（custom_level_<HASH>）から hash（小文字）
std::optional<std::string> hashOfLevelId(std::string const &levelId) {
  if (!levelId.starts_with(kCustomLevelPrefix)) return std::nullopt;
  return core::normalizeHash(levelId.substr(std::string(kCustomLevelPrefix).size(), 40));
}

// お気に入りと遊んだ回数を、ゲームの記録から集める。ゲームのスレッドで呼ぶ
core::JanitorInputs gatherInputs() {
  core::JanitorInputs inputs;
  auto models = UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::PlayerDataModel *>();
  auto data = models.size() > 0 && models[0] ? models[0]->get_playerData() : nullptr;
  if (!data || !data->get_favoritesLevelIds() || !data->get_levelsStatsData()) {
    inputs.unreadable = "ゲームの記録（お気に入りと遊んだ回数）が読めない";
    return inputs;
  }
  auto favorites = data->get_favoritesLevelIds()->GetEnumerator();
  while (favorites.MoveNext()) {
    if (auto hash = hashOfLevelId(static_cast<std::string>(favorites.get_Current()))) inputs.liked.insert(*hash);
  }
  // 遊んだ回数は、そのマップの全難易度の回数の合計（ゲームはクリアと失敗のたびに数える）
  auto stats = data->get_levelsStatsData()->GetEnumerator();
  while (stats.MoveNext()) {
    auto entry = stats.get_Current().get_Value();
    if (!entry) continue;
    if (auto hash = hashOfLevelId(static_cast<std::string>(entry->get_levelID()))) inputs.plays[*hash] += entry->get_playCount();
  }
  return inputs;
}

void runTrials() {
  std::thread([] {
    try {
      // SongCore が曲を読んでいる最中にフォルダを動かさないよう、最初の読み込みが終わるのを待つ
      while (!onGameThread<bool>([] { return SongCore::API::Loading::AreSongsLoaded() && !SongCore::API::Loading::AreSongsRefreshing(); }).value_or(false)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      auto gathered = onGameThread<std::pair<core::JanitorInputs, std::filesystem::path>>([] { return std::pair(gatherInputs(), SongCore::API::Loading::GetPreferredCustomLevelPath()); });
      if (!gathered) {
        Log.warn("[janitor] the game thread did not answer; nothing is deleted this time");
        return;
      }
      auto &[inputs, levels] = *gathered;
      core::JanitorPaths paths{dataDirectory(), levels,
                               // PlaylistManager と PlaylistCore の置き場所（推測。ないものは飛ばし、ひとつもなければ何も消さない）
                               {"/sdcard/ModData/com.beatgames.beatsaber/Mods/PlaylistManager/Playlists", "/sdcard/ModData/com.beatgames.beatsaber/Mods/PlaylistCore/Playlists"}};
      // 置き場所が外れていると守りが黙って効かないので、試し運転のログで確かめられるようにする
      for (auto const &directory : paths.playlists) {
        std::string playlistError;
        std::error_code exists;
        if (!std::filesystem::is_directory(directory, exists)) Log.info("[janitor] playlists: {} (none)", directory.string());
        else if (auto hashes = core::playlistHashes(directory, playlistError)) Log.info("[janitor] playlists: {} ({} maps protected)", directory.string(), hashes->size());
      }
      auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
      auto days = core::LaunchDays(paths.root).record(core::localDate(now));
      if (days) inputs.launchDays = *days;
      else if (inputs.unreadable.empty()) inputs.unreadable = "起動日の記録（state.json）が読めない";
      // 消すのは、メインメニューの画面が出ているあいだだけ（選曲やプレイの最中にマップを動かさない。OctantPath 法 2.6）
      auto leftMenu = [] { return !onGameThread<bool>([] { return mainMenuShown() && !inGameplay(); }).value_or(false); };
      auto report = core::runJanitor(paths, settings(), inputs, [] { return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()); }, leftMenu);
      for (auto const &line : report.lines) Log.info("[janitor] {}", line);
      if (report.stopped) Log.warn("[janitor] stopped: {}", report.stopReason);
      Log.info("[janitor] done ({}, {} decisions, {} launch days)", report.dryRun ? "dry run" : "live", report.lines.size(), inputs.launchDays.size());
      if (report.levelsChanged) BSML::MainThreadScheduler::Schedule([] { refreshSongsWhenIdle(nullptr); });
    } catch (std::exception const &e) {
      // 片付けに失敗しても、ゲームは妨げない
      Log.error("[janitor] {}", e.what());
    }
  }).detach();
}

MAKE_HOOK_MATCH(MainMenuDidActivate, &GlobalNamespace::MainMenuViewController::DidActivate, void, GlobalNamespace::MainMenuViewController *self, bool firstActivation,
                bool addedToHierarchy, bool screenSystemEnabling) {
  MainMenuDidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);
  rememberMainMenu(self);
  static bool ran = false;
  if (ran) return;
  ran = true;
  try {
    runTrials();
  } catch (std::exception const &e) {
    Log.error("[janitor] {}", e.what());
  }
}

} // namespace

void recordInstalled(core::OwnedMap const &map) {
  // 起動時の片付けが記録の鍵を持っていることがあるので、ゲームのスレッドを待たせない
  std::thread([map] {
    try {
      if (core::OwnershipLedger(dataDirectory()).add(map)) return;
      Log.warn("ledger.json could not be read; {} is not recorded", map.hash);
    } catch (std::exception const &e) {
      Log.warn("ledger.json could not be written: {}", e.what());
    }
  }).detach();
}

void installTrials() { INSTALL_HOOK(Log, MainMenuDidActivate); }

} // namespace octantpath
