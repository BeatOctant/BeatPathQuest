#pragma once

// 選曲画面の上に浮かぶ推薦の板（PC 版の RecommendationTab と同じ置き場所）。上から、起点、手札の選択欄と頼むボタン、
// 知らせの 1 行、推薦の一覧（カバー・曲名・作者）。一覧の行を押すと、導入済みの曲は選曲画面で選ぶ
namespace octantpath::ui {

// 選曲画面が開いたら板を出し、閉じたら隠す（late_load で 1 度だけ呼ぶ）
void installRecommendPanel();

// 接続の状態や起点が変わったので、手札の選択欄を作り直す。ゲームのスレッドで呼ぶ
void refreshRecommendPanel();

} // namespace octantpath::ui
