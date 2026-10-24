#include "levels.hpp"

#include "main.hpp"

#include "octantpath/core/beatsaver.hpp"

#include "songcore/shared/SongCore.hpp"

#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"

#include <mutex>

namespace octantpath {

namespace {

std::mutex selectedMutex;
std::optional<SelectedLevel> selected;

} // namespace

std::string difficultyName(int difficulty) {
  static char const *const names[] = {"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
  return difficulty >= 0 && difficulty < 5 ? names[difficulty] : std::to_string(difficulty);
}

void watchSelectedLevel(std::function<void()> changed) {
  SongCore::API::LevelSelect::GetLevelWasSelectedEvent() += [changed](SongCore::API::LevelSelect::LevelWasSelectedEventArgs const &args) {
    // 推薦の起点は BeatSaver の版の hash で決まる譜面だけ（公式の曲と WIP は起点にしない）
    if (!args.isCustom || args.isWIP || !args.hash || !args.beatmapLevel) return;
    // SongCore の hash は大文字なので、小文字にそろえる（OctantPath 法 1.2）
    auto hash = core::normalizeHash(*args.hash);
    auto key = args.beatmapKey;
    if (!hash || !key.beatmapCharacteristic) return;
    SelectedLevel level{
        .chart = {*hash, static_cast<std::string>(key.beatmapCharacteristic->get_serializedName()), difficultyName(static_cast<int>(key.difficulty.value__))},
        .songName = static_cast<std::string>(args.beatmapLevel->songName),
    };
    {
      std::lock_guard lock(selectedMutex);
      selected = std::move(level);
    }
    if (changed) BSML::MainThreadScheduler::Schedule(changed);
  };
}

std::optional<SelectedLevel> selectedLevel() {
  std::lock_guard lock(selectedMutex);
  return selected;
}

std::optional<InstalledLevel> installedLevel(std::string const &hash) {
  if (!SongCore::API::Loading::AreSongsLoaded()) return std::nullopt;
  auto level = SongCore::API::Loading::GetLevelByHash(hash);
  if (!level) return std::nullopt;
  InstalledLevel info{static_cast<std::string>(level->songName), static_cast<std::string>(level->songAuthorName), {}};
  ArrayW<StringW> mappers = level->allMappers;
  if (mappers) {
    for (auto mapper : mappers) info.mappers.push_back(static_cast<std::string>(mapper));
  }
  return info;
}

} // namespace octantpath
