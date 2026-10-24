#pragma once

#include "octantpath/core/settings.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// マップの zip を作業用の場所で展開と検証をして、通ったものだけを置き場所に移す（OctantPath 法 2.1〜2.4。PC 版の MapInstaller と MapHash と同じ）
namespace octantpath::core {

enum class InstallFailure {
  None,
  BadHash,           // 頼んだ hash の形が違う
  BadFolderName,     // フォルダ名に使えない
  NameTaken,         // 置き場所に同じ名前がある
  ZipTooLarge,       // zip が zipMaxDownloadMegabytes を超える
  ZipUnreadable,     // zip として読めない
  TooManyEntries,    // 中身の数が zipMaxEntries を超える
  BadEntryName,      // マップのフォルダの外を指す名前がある
  NoInfo,            // Info.dat がない
  ManyRoots,         // Info.dat が 2 つ以上ある
  ExtractedTooLarge, // 展開した大きさが zipMaxExtractedMegabytes を超える
  InfoUnreadable,    // Info.dat が JSON として読めない
  InfoMalformed,     // Info.dat の形が違う
  InfoPointsOutside, // Info.dat がフォルダの外を指す
  InfoFileMissing,   // Info.dat が指すファイルがない
  HashMismatch,      // 展開したマップの hash が頼んだ版と違う
  Io,                // 書き込みや移動に失敗した
};

struct MapHashResult {
  // 40 桁の小文字の 16 進。失敗なら空
  std::string hash;
  // hash に入れたファイル（Info.dat が先頭）
  std::vector<std::string> files;
  InstallFailure failure = InstallFailure::None;
  std::string detail;
};

// マップの版の hash。ZIP 全体ではなく、Info.dat とそれが指すファイルを決まった順に連結した SHA-1（SongCore と同じ順）。
// Info.dat が v4 なら音源、続いて難易度ごとに譜面とライティング。それより前の形式は難易度の譜面
MapHashResult computeMapHash(std::filesystem::path const &folder);

// マップのフォルダの中だけを指す名前か（OctantPath 法 2.3）
bool safeEntryName(std::string const &name);
bool safeFolderName(std::string const &name);
// BeatSaver の key と曲名から、フォルダ名に使える名前を作る（PC 版と同じ形: "key (曲名 - マッパー)"、80 文字まで）
std::string folderName(std::string const &key, std::string const &songName, std::string const &mapper);

struct InstallResult {
  std::filesystem::path path;
  std::string hash;
  InstallFailure failure = InstallFailure::None;
  std::string detail;

  bool ok() const { return failure == InstallFailure::None; }
};

// zip（中身のバイト列）を destination に展開する。マップの根（Info.dat のある場所）の中だけを出す
InstallResult extractMapZip(std::string const &zip, std::filesystem::path const &destination, Settings const &settings);

// 頼んだ hash の版の zip を、workDirectory で展開して確かめ、levelsDirectory/folderName に移す。
// levelsDirectory と workDirectory は同じファイルシステムに置く（移すときに途中の状態を見せないため）。片付けるのは、この処理で作ったものだけ
InstallResult installMap(std::string const &expectedHash, std::string const &zip, std::string const &folderName, std::filesystem::path const &levelsDirectory,
                         std::filesystem::path const &workDirectory, Settings const &settings);

} // namespace octantpath::core
