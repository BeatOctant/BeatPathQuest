#include "game.hpp"

#include "songcore/shared/SongCore.hpp"

#include "GlobalNamespace/GameScenesManager.hpp"
#include "UnityEngine/Resources.hpp"

#include <thread>

namespace octantpath {

namespace {

UnityW<GlobalNamespace::MainMenuViewController> mainMenu;
UnityW<GlobalNamespace::GameScenesManager> scenes;

constexpr float kIdleCheckSeconds = 1.0f;

} // namespace

bool inGameplay() {
  if (!scenes) {
    auto found = UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::GameScenesManager *>();
    if (found.size() > 0) scenes = found[0];
  }
  // 場面の管理が見つからないのは起動の途中だけなので、プレイ中ではない
  if (!scenes) return false;
  return scenes->get_isInTransition() || scenes->IsSceneInStack("GameCore");
}

void rememberMainMenu(GlobalNamespace::MainMenuViewController *menu) { mainMenu = menu; }

bool mainMenuShown() { return mainMenu && mainMenu->get_isActivated(); }

void refreshSongsWhenIdle(std::function<void()> done) {
  if (inGameplay()) {
    BSML::MainThreadScheduler::ScheduleAfterTime(kIdleCheckSeconds, [done] { refreshSongsWhenIdle(done); });
    return;
  }
  auto refreshed = SongCore::API::Loading::RefreshSongs(false);
  std::thread([refreshed, done] {
    if (refreshed.valid()) refreshed.wait();
    if (done) BSML::MainThreadScheduler::Schedule(done);
  }).detach();
}

void waitUntilNotPlaying() {
  while (onGameThread<bool>([] { return inGameplay(); }).value_or(true)) std::this_thread::sleep_for(std::chrono::seconds(1));
}

} // namespace octantpath
