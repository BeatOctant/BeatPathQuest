#pragma once

#include "octantpath/core/connection.hpp"

#include <string>

namespace octantpath::core {

// 画面の言語。"auto" は MOD が機器の言語から決める
enum class Language { Ja, En };

Language resolveLanguage(std::string const &setting, std::string const &deviceLocale);

// 接続状態を画面に出す文
std::string describe(ConnectionStatus const &status, Language language);

} // namespace octantpath::core
