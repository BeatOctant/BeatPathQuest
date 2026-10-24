#include "octantpath/core/ledger.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace octantpath::core {

namespace {

std::optional<json> readJson(std::filesystem::path const &path) {
  std::ifstream file(path);
  if (!file) return std::nullopt;
  std::stringstream text;
  text << file.rdbuf();
  return json::parse(text.str());
}

std::string text(json const &object, char const *key) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) return {};
  return it->get<std::string>();
}

std::optional<std::string> optionalText(json const &object, char const *key) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) return std::nullopt;
  return it->get<std::string>();
}

json toJson(FrameEntry const &frame) {
  json object = {{"frame", frame.frame}, {"requestId", frame.requestId}, {"characteristic", frame.characteristic}, {"difficulty", frame.difficulty}, {"at", frame.at}};
  if (!frame.preset.empty()) object["preset"] = frame.preset;
  return object;
}

json toJson(OwnedMap const &map) {
  json frames = json::array();
  for (auto const &frame : map.frames) frames.push_back(toJson(frame));
  json object = {{"hash", map.hash}, {"mapKey", map.mapKey}, {"folder", map.folder}, {"acquiredAt", map.acquiredAt}, {"state", map.state}, {"frames", frames}};
  if (map.changedAt) object["changedAt"] = *map.changedAt;
  if (map.keptFolder) object["keptFolder"] = *map.keptFolder;
  return object;
}

OwnedMap ownedMap(json const &object) {
  OwnedMap map;
  map.hash = text(object, "hash");
  map.mapKey = text(object, "mapKey");
  map.folder = text(object, "folder");
  map.acquiredAt = text(object, "acquiredAt");
  map.state = text(object, "state");
  if (map.hash.empty() || map.folder.empty() || map.state.empty() || !parseUtc(map.acquiredAt)) throw std::runtime_error("a map lacks hash, folder, state or acquiredAt");
  if (auto it = object.find("frames"); it != object.end() && !it->is_null()) {
    for (auto const &item : it->get<std::vector<json>>()) {
      map.frames.push_back({text(item, "frame"), text(item, "requestId"), text(item, "preset"), text(item, "characteristic"), text(item, "difficulty"), text(item, "at")});
    }
  }
  map.changedAt = optionalText(object, "changedAt");
  map.keptFolder = optionalText(object, "keptFolder");
  return map;
}

} // namespace

void writeFileAtomically(std::filesystem::path const &path, std::string const &contents) {
  std::filesystem::create_directories(path.parent_path());
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    file << contents;
    file.flush();
    if (!file) throw std::runtime_error(path.filename().string() + " could not be written");
  }
  std::filesystem::rename(temporary, path);
}

OwnershipLedger::OwnershipLedger(std::filesystem::path directory) : path_(std::move(directory) / kFileName) {}

std::optional<LedgerFile> OwnershipLedger::load() const {
  std::error_code exists;
  // あるかどうか分からないときは、ないとみなさない（空の記録で上書きしないため）
  if (!std::filesystem::exists(path_, exists)) return exists ? std::nullopt : std::optional(LedgerFile{});
  try {
    auto object = readJson(path_);
    if (!object || !object->is_object()) return std::nullopt;
    auto version = object->find("formatVersion");
    if (version == object->end() || !version->is_number_integer() || version->get<int>() != 1) return std::nullopt;
    auto maps = object->find("maps");
    if (maps == object->end() || !maps->is_array()) return std::nullopt;
    LedgerFile ledger;
    for (auto const &item : *maps) ledger.maps.push_back(ownedMap(item));
    return ledger;
  } catch (std::exception const &) {
    return std::nullopt;
  }
}

void OwnershipLedger::save(LedgerFile const &ledger) const {
  json maps = json::array();
  for (auto const &map : ledger.maps) maps.push_back(toJson(map));
  writeFileAtomically(path_, json{{"formatVersion", ledger.formatVersion}, {"maps", maps}}.dump(2));
}

std::mutex &OwnershipLedger::mutex() {
  static std::mutex lock;
  return lock;
}

bool OwnershipLedger::add(OwnedMap const &map) const {
  std::lock_guard lock(mutex());
  auto ledger = load();
  if (!ledger) return false;
  std::erase_if(ledger->maps, [&](OwnedMap const &m) { return m.hash == map.hash && m.folder == map.folder; });
  ledger->maps.push_back(map);
  save(*ledger);
  return true;
}

LaunchDays::LaunchDays(std::filesystem::path directory) : path_(std::move(directory) / kFileName) {}

std::optional<std::vector<std::string>> LaunchDays::load() const {
  std::error_code exists;
  if (!std::filesystem::exists(path_, exists)) return std::vector<std::string>{};
  try {
    auto object = readJson(path_);
    if (!object || !object->is_object()) return std::nullopt;
    auto days = object->find("launchDays");
    if (days == object->end() || days->is_null()) return std::vector<std::string>{};
    return days->get<std::vector<std::string>>();
  } catch (std::exception const &) {
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>> LaunchDays::record(std::string const &today) const {
  auto days = load();
  if (!days) return std::nullopt;
  if (std::find(days->begin(), days->end(), today) == days->end()) {
    days->push_back(today);
    writeFileAtomically(path_, json{{"launchDays", *days}}.dump(2));
  }
  return days;
}

std::string formatUtc(UtcTime time) {
  auto days = std::chrono::floor<std::chrono::days>(time);
  std::chrono::year_month_day date{days};
  std::chrono::hh_mm_ss clock{time - days};
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<int>(date.year()), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                static_cast<long long>(clock.hours().count()), static_cast<long long>(clock.minutes().count()), static_cast<long long>(clock.seconds().count()));
  return buffer;
}

std::optional<UtcTime> parseUtc(std::string const &text) {
  int year, month, day, hour, minute, second;
  char t;
  if (text.size() < 19 || std::sscanf(text.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &year, &month, &day, &t, &hour, &minute, &second) != 7 || (t != 'T' && t != ' ')) return std::nullopt;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return std::nullopt;
  // 月の日数を超える日（2 月 31 日など）は、これまでどおり翌月へ繰り越す
  std::chrono::sys_days date = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} / 1;
  return UtcTime(date) + std::chrono::days{day - 1} + std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
}

std::string localDate(UtcTime time) {
  std::time_t seconds = static_cast<std::time_t>(time.time_since_epoch().count());
  std::tm local{};
  localtime_r(&seconds, &local);
  char buffer[16];
  std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &local);
  return buffer;
}

} // namespace octantpath::core
