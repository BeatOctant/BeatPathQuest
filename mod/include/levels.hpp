#pragma once

#include "octantpath/core/recommendations.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// ゲームの中の譜面（SongCore）。推薦の起点と、推薦された譜面が導入済みかを見る
namespace octantpath {

struct SelectedLevel {
  core::ChartRef chart;
  std::string songName;
};

// 選曲で最後に選んだカスタムの譜面を覚え始める（late_load で 1 度だけ呼ぶ）。変わるたびに changed をゲームのスレッドで呼ぶ
void watchSelectedLevel(std::function<void()> changed);

// 最後に選んだカスタムの譜面。どのスレッドから呼んでもよい
std::optional<SelectedLevel> selectedLevel();

struct InstalledLevel {
  std::string songName;
  std::string songAuthorName;
  std::vector<std::string> mappers;
};

// hash の版が導入済みなら、その曲の情報。ゲームのスレッドで呼ぶ
std::optional<InstalledLevel> installedLevel(std::string const &hash);

// BeatSaber の難易度の名前（契約3 の difficulty: Easy、Normal、Hard、Expert、ExpertPlus）
std::string difficultyName(int difficulty);

} // namespace octantpath
