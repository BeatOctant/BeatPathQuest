#pragma once

#include <string>
#include <vector>

namespace octantpath::core {

// GET /v1/presets の応答（OctantSpecs plans/presets.md）。中の条件は OctantVector が持ち、ここには来ない。
// 表示名（label・hint・group と入力・選択肢の label）は、文字列か {言語: 文字列}。読むときに今の言語の文字列にする
struct PresetOption {
  std::string value;
  std::string label;
};

struct PresetInput {
  static constexpr char const *kSeed = "seed";
  static constexpr char const *kChoice = "choice";

  std::string kind;
  bool optional = false;
  std::string name;
  std::string label;
  std::vector<PresetOption> options;
};

struct Preset {
  std::string id;
  std::string label;
  std::string hint;
  // 任意。ボタンをまとめる見出し
  std::string group;
  std::vector<PresetInput> inputs;
};

struct PresetsResponse {
  std::string revision;
  std::vector<Preset> presets;
};

// language は "ja" や "en"。{言語: 文字列} の表示名は、その言語、なければ英語、なければ最初の文字列を使う
PresetsResponse parsePresets(std::string const &text, std::string const &language);

} // namespace octantpath::core
