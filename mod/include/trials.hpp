#pragma once

#include "octantpath/core/ledger.hpp"

// お試し中のマップ（OctantPath が入れた曲）の記録と片付け
namespace octantpath {

// 入れた曲を所有の記録に足す。SongCore が読み込んだあとにだけ呼ぶ（OctantPath 法 3.1）。書くのは別のスレッドで、足せなかったらログに書く
void recordInstalled(core::OwnedMap const &map);

// メインメニューが最初に開いたら、SongCore の最初の読み込みが終わるのを待って、起動日を記録してお試し中のマップを残すか消す。
// 消すのはメインメニューの画面が出ているあいだだけで、離れたら残りは次の起動で決める（late_load で 1 度だけ呼ぶ）
void installTrials();

} // namespace octantpath
