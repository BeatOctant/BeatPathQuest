#include "octantpath/core/panel_placement.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace octantpath::core;

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() / ("octantpath-panel-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() { std::filesystem::remove_all(path); }
};

void write(std::filesystem::path const &path, char const *text) { std::ofstream(path) << text; }

} // namespace

TEST_CASE("the panel placement is saved and read back") {
  TempDir dir;
  PanelPlacementStore store(dir.path, nullptr);
  CHECK_FALSE(store.load());
  store.save({{0.5f, 2.25f, 3.0f}, {0.1f, 0.2f, 0.3f, 0.9f}});
  auto loaded = store.load();
  REQUIRE(loaded);
  CHECK(loaded->position == std::array<float, 3>{0.5f, 2.25f, 3.0f});
  CHECK(loaded->rotation == std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.9f});
  CHECK_FALSE(std::filesystem::exists(dir.path / "panel.json.tmp"));
}

TEST_CASE("an unreadable panel placement falls back to the default position") {
  TempDir dir;
  std::vector<std::string> warnings;
  PanelPlacementStore store(dir.path, [&](std::string const &message) { warnings.push_back(message); });
  for (auto text : {"{", R"({"position": [1, 2], "rotation": [0, 0, 0, 1]})", R"({"position": [1, 2, "3"], "rotation": [0, 0, 0, 1]})",
                    R"({"position": [1, 2, 3]})"}) {
    write(dir.path / "panel.json", text);
    CHECK_FALSE(store.load());
  }
  CHECK(warnings.size() == 4);
}
