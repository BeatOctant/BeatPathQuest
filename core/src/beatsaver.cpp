#include "octantpath/core/beatsaver.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

namespace octantpath::core {

namespace {

using nlohmann::json;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}

std::string str(json const &object, char const *key) {
  auto it = object.find(key);
  return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<double> num(json const &object, char const *key) {
  auto it = object.find(key);
  return it != object.end() && it->is_number() ? std::optional(it->get<double>()) : std::nullopt;
}

BeatSaverMap mapOf(json const &object) {
  BeatSaverMap map;
  map.key = str(object, "id");
  map.name = str(object, "name");
  if (auto metadata = object.find("metadata"); metadata != object.end() && metadata->is_object()) {
    map.metadata = {str(*metadata, "songName"), str(*metadata, "songSubName"), str(*metadata, "songAuthorName"), str(*metadata, "levelAuthorName"), num(*metadata, "duration").value_or(0)};
  }
  if (auto versions = object.find("versions"); versions != object.end() && versions->is_array()) {
    for (auto const &v : *versions) {
      BeatSaverVersion version{str(v, "hash"), str(v, "state"), str(v, "downloadURL"), str(v, "coverURL"), str(v, "previewURL"), {}};
      if (auto diffs = v.find("diffs"); diffs != v.end() && diffs->is_array()) {
        for (auto const &d : *diffs) version.diffs.push_back({str(d, "characteristic"), str(d, "difficulty"), num(d, "nps"), num(d, "njs")});
      }
      map.versions.push_back(std::move(version));
    }
  }
  return map;
}

} // namespace

BeatSaverVersion const *BeatSaverMap::version(std::string const &hash) const {
  auto wanted = normalizeHash(hash);
  if (!wanted) return nullptr;
  for (auto const &v : versions) {
    if (normalizeHash(v.hash) == wanted) return &v;
  }
  return nullptr;
}

std::optional<std::string> normalizeHash(std::string const &hash) {
  if (hash.size() != 40 || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c); })) return std::nullopt;
  return lower(hash);
}

bool allowedBeatSaverUrl(std::string const &url) {
  static std::string const scheme = "https://";
  if (url.compare(0, scheme.size(), scheme) != 0) return false;
  auto rest = url.substr(scheme.size());
  auto end = rest.find_first_of("/?#");
  auto authority = rest.substr(0, end);
  if (authority.find('@') != std::string::npos) return false;
  auto host = lower(authority.substr(0, authority.find(':')));
  while (!host.empty() && host.back() == '.') host.pop_back();
  static std::string const domain = "beatsaver.com";
  static std::string const suffix = ".beatsaver.com";
  return host == domain || (host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0);
}

BeatSaverMap parseBeatSaverMap(std::string const &text) { return mapOf(json::parse(text)); }

std::map<std::string, BeatSaverMap> parseMapsByHash(std::string const &text, std::vector<std::string> const &asked) {
  auto object = json::parse(text);
  std::map<std::string, BeatSaverMap> result;
  if (asked.size() == 1) {
    if (object.is_object() && object.contains("versions")) result[asked[0]] = mapOf(object);
    return result;
  }
  if (!object.is_object()) return result;
  for (auto const &[name, value] : object.items()) {
    auto hash = normalizeHash(name);
    if (hash && value.is_object()) result[*hash] = mapOf(value);
  }
  return result;
}

std::string percentEncode(std::string const &text) {
  static char const *const hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

BeatSaverClient::BeatSaverClient(Transport &transport, std::function<Settings()> settings, Sleep sleep)
    : transport_(transport), settings_(std::move(settings)), sleep_(sleep ? std::move(sleep) : Sleep([](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); })) {}

BeatSaverClient::Lookup BeatSaverClient::mapsByHash(std::vector<std::string> const &hashes) {
  Lookup lookup;
  std::vector<std::string> wanted;
  for (auto const &h : hashes) {
    auto hash = normalizeHash(h);
    if (hash && std::find(wanted.begin(), wanted.end(), *hash) == wanted.end()) wanted.push_back(*hash);
  }
  for (size_t i = 0; i < wanted.size(); i += kMaxHashesPerRequest) {
    std::vector<std::string> chunk(wanted.begin() + i, wanted.begin() + std::min(wanted.size(), i + kMaxHashesPerRequest));
    std::string joined;
    for (auto const &h : chunk) joined += (joined.empty() ? "" : ",") + h;
    try {
      auto text = get("maps/hash/" + joined);
      if (!text) continue;
      for (auto &[hash, map] : parseMapsByHash(*text, chunk)) lookup.maps[hash] = std::move(map);
    } catch (std::exception const &e) {
      lookup.error = e.what();
    }
  }
  return lookup;
}

std::optional<std::string> BeatSaverClient::download(std::string const &url, std::string *error) {
  auto fail = [error](std::string message) -> std::optional<std::string> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  if (!allowedBeatSaverUrl(url)) return fail("not a BeatSaver URL: " + url);
  auto settings = settings_();
  Endpoint proxy{settings.beatSaverProxyHost, settings.beatSaverProxyPort};
  auto zipMaxBytes = static_cast<size_t>(std::max(0, settings.zipMaxDownloadMegabytes)) * 1024 * 1024;
  auto response = transport_.send(proxy, {"GET", "/v1/beatsaver?url=" + percentEncode(url), {}, {}, zipMaxBytes}, std::chrono::seconds(settings.cdnTimeoutSeconds));
  if (!response.reached) return fail("the PC proxy is unreachable (" + proxy.url() + "): " + response.error);
  if (response.status < 200 || response.status >= 300) return fail("HTTP " + std::to_string(response.status));
  if (response.body.size() > static_cast<size_t>(settings.zipMaxDownloadMegabytes) * 1024 * 1024) return fail("too large");
  return std::move(response.body);
}

std::optional<std::string> BeatSaverClient::mapZip(std::string const &hash, BeatSaverVersion const *version, std::string *error) {
  auto fail = [error](std::string message) -> std::optional<std::string> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  auto normalized = normalizeHash(hash);
  if (!normalized) return fail("bad hash: " + hash);
  auto settings = settings_();
  Endpoint proxy{settings.beatSaverProxyHost, settings.beatSaverProxyPort};
  auto zipMaxBytes = static_cast<size_t>(std::max(0, settings.zipMaxDownloadMegabytes)) * 1024 * 1024;
  auto local = transport_.send(proxy, {"GET", "/v1/local-zip/" + *normalized, {}, {}, zipMaxBytes}, std::chrono::seconds(settings.cdnTimeoutSeconds));
  if (!local.reached) return fail("the PC proxy is unreachable (" + proxy.url() + "): " + local.error);
  if (local.status >= 200 && local.status < 300) return std::move(local.body);
  if (local.status != 404 || local.body.find("\"no_local_zip\"") == std::string::npos) {
    return fail(local.status == 404 ? "the zip is not in the PC's zip folder" : "the PC proxy: HTTP " + std::to_string(local.status));
  }
  if (!version || version->downloadUrl.empty()) return fail("BeatSaver has no download for this version");
  return download(version->downloadUrl, error);
}

std::optional<std::string> BeatSaverClient::get(std::string const &path) {
  std::lock_guard lock(serial_);
  auto settings = settings_();
  // 間隔と最大の待ちは 1 秒より短くしない（PC 版と同じ。0 だと 429 の再試行で間隔が延びない）
  auto interval = std::max(kMinApiInterval, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(settings.beatSaverApiIntervalSeconds)));
  auto maxBackoff = std::max<std::chrono::milliseconds>(kMinApiInterval, std::chrono::seconds(settings.beatSaverApiMaxBackoffSeconds));
  auto backoff = interval;
  Endpoint proxy{settings.beatSaverProxyHost, settings.beatSaverProxyPort};
  auto url = settings.beatSaverApiUrl + path;
  while (true) {
    auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(lastRequest_ + interval - std::chrono::steady_clock::now());
    if (wait.count() > 0) sleep_(wait);
    lastRequest_ = std::chrono::steady_clock::now();

    auto response = transport_.send(proxy, {"GET", "/v1/beatsaver?url=" + percentEncode(url), {}, {}}, settings.hostRequestTimeout());
    if (!response.reached) throw std::runtime_error("the PC proxy is unreachable (" + proxy.url() + "): " + response.error);
    if (response.status == 429) {
      backoff *= 2;
      if (backoff > maxBackoff) throw std::runtime_error("BeatSaver: too many requests");
      sleep_(backoff);
      continue;
    }
    if (response.status == 404) {
      // プロキシ自身の 404（知らない道）と BeatSaver の 404（見つからない）を分ける
      if (response.body.find("\"not_found\"") != std::string::npos && response.body.find("unknown path") != std::string::npos)
        throw std::runtime_error("the PC proxy does not know /v1/beatsaver");
      return std::nullopt;
    }
    if (response.status < 200 || response.status >= 300) throw std::runtime_error("BeatSaver: HTTP " + std::to_string(response.status));
    return response.body;
  }
}

std::vector<RecommendationItem> recommendationItems(RecommendationResponse const &response, BeatSaverClient::Lookup const &lookup) {
  std::vector<RecommendationItem> items;
  for (auto const &candidate : response.candidates) {
    RecommendationItem item{.candidate = candidate, .hash = normalizeHash(candidate.hash).value_or("")};
    if (auto it = lookup.maps.find(item.hash); !item.hash.empty() && it != lookup.maps.end()) {
      item.map = it->second;
      if (auto version = it->second.version(item.hash)) {
        item.version = *version;
        for (auto const &diff : version->diffs) {
          if (diff.characteristic == candidate.characteristic && diff.difficulty == candidate.difficulty) {
            item.nps = diff.nps;
            item.njs = diff.njs;
            break;
          }
        }
      }
    }
    items.push_back(std::move(item));
  }
  return items;
}

} // namespace octantpath::core
