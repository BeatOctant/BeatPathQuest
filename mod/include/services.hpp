#pragma once

#include "octantpath/core/beatsaver.hpp"
#include "octantpath/core/connection.hpp"
#include "octantpath/core/posix_transport.hpp"
#include "octantpath/core/recommender.hpp"
#include "octantpath/core/status_text.hpp"

#include <filesystem>
#include <string>

// MOD の中で 1 つずつ持つ部品。コアの部品に、Quest での置き場所と送り方を差し込む
namespace octantpath {

// OctantPath の設定と接続情報の置き場所（PC 版の UserData/OctantPath/ にあたる）
std::filesystem::path dataDirectory();

core::Settings settings();
core::Language language();
core::HostConnection &connection();
core::Recommender &recommender();
// BeatSaver API を PC のプロキシ越しに引く口
core::BeatSaverClient &beatSaver();

// 機器の名前と言語（Android のシステムプロパティ）
std::string deviceModel();
std::string deviceLocale();

} // namespace octantpath
