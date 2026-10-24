#pragma once

#include "HMUI/ViewController.hpp"

// メインメニューの「OctantPath」の画面。接続の状態と、つなぐ先と合言葉のペアリング（契約6）。推薦は選曲画面の板（recommend_panel）で受け取る
namespace octantpath::ui {

void octantpathViewDidActivate(HMUI::ViewController *self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);

} // namespace octantpath::ui
