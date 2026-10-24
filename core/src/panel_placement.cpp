#include "octantpath/core/panel_placement.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace octantpath::core {

namespace {

template <size_t N> std::array<float, N> numbers(json const &value) {
  if (!value.is_array() || value.size() != N) throw std::runtime_error("wrong length");
  std::array<float, N> result{};
  for (size_t i = 0; i < N; ++i) {
    if (!value[i].is_number()) throw std::runtime_error("not a number");
    result[i] = value[i].get<float>();
    if (!std::isfinite(result[i])) throw std::runtime_error("not finite");
  }
  return result;
}

} // namespace

PanelPlacementStore::PanelPlacementStore(std::filesystem::path directory, std::function<void(std::string const &)> warn)
    : path_(std::move(directory) / kFileName), warn_(std::move(warn)) {}

std::optional<PanelPlacement> PanelPlacementStore::load() const {
  std::error_code exists;
  if (!std::filesystem::exists(path_, exists)) return std::nullopt;
  try {
    std::ifstream file(path_);
    std::stringstream text;
    text << file.rdbuf();
    auto object = json::parse(text.str());
    return PanelPlacement{numbers<3>(object.at("position")), numbers<4>(object.at("rotation"))};
  } catch (std::exception const &e) {
    if (warn_) warn_(std::string("panel.json could not be read: ") + e.what());
    return std::nullopt;
  }
}

void PanelPlacementStore::save(PanelPlacement const &placement) const {
  json object = {{"position", placement.position}, {"rotation", placement.rotation}};
  std::filesystem::create_directories(path_.parent_path());
  auto temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    file << object.dump(2);
    file.flush();
    if (!file) throw std::runtime_error("panel.json could not be written");
  }
  std::filesystem::rename(temporary, path_);
}

} // namespace octantpath::core
