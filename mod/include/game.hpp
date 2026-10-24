#pragma once

#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include "GlobalNamespace/MainMenuViewController.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>

// ゲームが今どこにいるか。プレイ中は、マップのファイルと SongCore の読み込みを変えない（OctantPath 法 2.6。PC 版の InGameplay と同じ）
namespace octantpath {

// --- ゲームのスレッドで呼ぶ ---

// プレイの場面（GameCore）にいるか、場面を移っている途中か
bool inGameplay();

// メインメニューの画面を覚える（MainMenuViewController の DidActivate で呼ぶ）
void rememberMainMenu(GlobalNamespace::MainMenuViewController *menu);
// メインメニューの画面が出ているか（起動時の片付けは、このあいだだけ消す）
bool mainMenuShown();

// SongCore に読み直させ、終わったら done をゲームのスレッドで呼ぶ。プレイ中なら、メニューに戻るまで待ってから読み直す
void refreshSongsWhenIdle(std::function<void()> done);

// --- ゲームのスレッドの外で呼ぶ ---

// ゲームのスレッドで work を呼んで、結果を待つ。timeout までに返らなければ nullopt
template <class T> std::optional<T> onGameThread(std::function<T()> work, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto promise = std::make_shared<std::promise<T>>();
  auto future = promise->get_future();
  BSML::MainThreadScheduler::Schedule([promise, work = std::move(work)] {
    try {
      promise->set_value(work());
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  });
  if (future.wait_for(timeout) != std::future_status::ready) return std::nullopt;
  return future.get();
}

// プレイ中でなくなるまで待つ（展開や照合をプレイ中にしない）。分からないときもプレイ中とみなして待つ
void waitUntilNotPlaying();

} // namespace octantpath
