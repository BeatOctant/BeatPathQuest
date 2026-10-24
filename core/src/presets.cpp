#include "octantpath/core/presets.hpp"

#include "octantpath/core/beatsaver.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace octantpath::core {

namespace {

using nlohmann::json;

bool sameLanguage(std::string const &a, std::string const &b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

// 文字列か {言語: 文字列}。PC 版の Lang.Pick と同じく、今の言語、英語、最初の文字列の順
std::string pick(json const &object, char const *key, std::string const &language) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) return {};
  if (it->is_string()) return it->get<std::string>();
  if (it->is_object()) {
    for (auto const &wanted : {language, std::string("en")}) {
      for (auto const &[name, value] : it->items()) {
        if (value.is_string() && sameLanguage(name, wanted)) return value.get<std::string>();
      }
    }
    for (auto const &[name, value] : it->items()) {
      if (value.is_string()) return value.get<std::string>();
    }
    return {};
  }
  return it->dump();
}

} // namespace

PresetsResponse parsePresets(std::string const &text, std::string const &language) {
  auto object = json::parse(text);
  PresetsResponse response;
  response.revision = object.value("revision", "");
  for (auto const &item : object.at("presets")) {
    Preset preset;
    preset.id = item.value("id", "");
    preset.label = pick(item, "label", language);
    preset.hint = pick(item, "hint", language);
    preset.group = pick(item, "group", language);
    if (auto inputs = item.find("inputs"); inputs != item.end() && inputs->is_array()) {
      for (auto const &in : *inputs) {
        PresetInput input;
        input.kind = in.value("kind", "");
        input.optional = in.value("optional", false);
        input.name = in.value("name", "");
        input.label = pick(in, "label", language);
        if (auto options = in.find("options"); options != in.end() && options->is_array()) {
          for (auto const &option : *options) input.options.push_back({option.value("value", ""), pick(option, "label", language)});
        }
        preset.inputs.push_back(std::move(input));
      }
    }
    response.presets.push_back(std::move(preset));
  }
  return response;
}

namespace preset_rules {

bool isShown(Preset const &preset, bool hasSeedContext) {
  if (preset.id.empty() || preset.label.empty()) return false;
  for (auto const &input : preset.inputs) {
    if (input.optional) continue;
    if (input.kind == PresetInput::kSeed) {
      if (!hasSeedContext) return false;
    } else if (input.kind == PresetInput::kChoice) {
      if (input.name.empty() || input.options.empty()) return false;
    } else {
      return false;
    }
  }
  return true;
}

std::vector<Preset> shown(std::vector<Preset> const &presets, bool hasSeedContext) {
  std::vector<Preset> result;
  std::copy_if(presets.begin(), presets.end(), std::back_inserter(result), [&](Preset const &p) { return isShown(p, hasSeedContext); });
  return result;
}

bool needsSeed(Preset const &preset) {
  return std::any_of(preset.inputs.begin(), preset.inputs.end(), [](PresetInput const &i) { return i.kind == PresetInput::kSeed && !i.optional; });
}

bool usesSeed(Preset const &preset) {
  return std::any_of(preset.inputs.begin(), preset.inputs.end(), [](PresetInput const &i) { return i.kind == PresetInput::kSeed; });
}

std::vector<PresetInput> choices(Preset const &preset) {
  std::vector<PresetInput> result;
  std::copy_if(preset.inputs.begin(), preset.inputs.end(), std::back_inserter(result), [](PresetInput const &i) { return i.kind == PresetInput::kChoice && !i.optional; });
  return result;
}

RecommendationRequest request(Preset const &preset, std::optional<ChartRef> const &seed, std::map<std::string, std::string> const &choices, int count) {
  RecommendationRequest request;
  request.preset = preset.id;
  request.choices = choices;
  if (usesSeed(preset) && seed) {
    // hash は小文字にそろえて送る（OctantPath 法 1.2。SongCore は大文字で渡し、OctantCompass の DB は小文字）
    request.seed = seed;
    request.seed->hash = normalizeHash(seed->hash).value_or(seed->hash);
  }
  request.count = count;
  return request;
}

} // namespace preset_rules

bool PresetsCache::needsReload(ConnectionStatus const &status) const {
  return status.state == ConnectionState::Ready && (!presets_ || (status.presetsRevision && *status.presetsRevision != presets_->revision));
}

bool PresetsCache::update(HostResult<PresetsResponse> const &result) {
  if (result.ok()) {
    presets_ = result.body;
    failure_.clear();
    return true;
  }
  failure_ = result.detailOrStatus();
  return false;
}

} // namespace octantpath::core
