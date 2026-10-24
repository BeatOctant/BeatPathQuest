#include "octantpath/core/connection.hpp"

#include "octantpath/core/contract_version.hpp"
#include "octantpath/core/ledger.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

#include <arpa/inet.h>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace octantpath::core {

namespace {

std::string bracketIPv6(std::string const &host) { return host.find(':') != std::string::npos && !host.starts_with('[') ? "[" + host + "]" : host; }

bool isIPv6(std::string const &text) {
  in6_addr address;
  return inet_pton(AF_INET6, text.c_str(), &address) == 1;
}

bool isHostName(std::string const &text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; });
}

std::optional<int> parsePort(std::string const &text) {
  int port = 0;
  if (text.empty()) return std::nullopt;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
  if (error != std::errc{} || end != text.data() + text.size() || port < 1 || port > 65535) return std::nullopt;
  return port;
}

} // namespace

bool ConnectionInfo::isLoopback(std::string const &host) {
  std::string h = host;
  if (h.starts_with('[') && h.ends_with(']')) h = h.substr(1, h.size() - 2);
  return h == "localhost" || h == "::1" || h.starts_with("127.");
}

std::string ConnectionInfo::address() const {
  if (!host || host->empty()) return "";
  return port ? bracketIPv6(*host) + ":" + std::to_string(*port) : bracketIPv6(*host);
}

std::optional<ParsedAddress> parseAddress(std::string const &input) {
  auto begin = input.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return std::nullopt;
  auto text = input.substr(begin, input.find_last_not_of(" \t\r\n") - begin + 1);

  // 角括弧の IPv6（"[fe80::1]" か "[fe80::1]:23287"）
  if (text.starts_with('[')) {
    auto close = text.find(']');
    if (close == std::string::npos) return std::nullopt;
    auto host = text.substr(1, close - 1);
    if (!isIPv6(host)) return std::nullopt;
    auto rest = text.substr(close + 1);
    if (rest.empty()) return ParsedAddress{host, std::nullopt};
    if (!rest.starts_with(':')) return std::nullopt;
    auto port = parsePort(rest.substr(1));
    if (!port) return std::nullopt;
    return ParsedAddress{host, port};
  }
  // 角括弧のない IPv6 はポートを付けられないので、そのままアドレスとして読む
  if (std::count(text.begin(), text.end(), ':') >= 2) {
    if (!isIPv6(text)) return std::nullopt;
    return ParsedAddress{text, std::nullopt};
  }
  auto colon = text.find(':');
  auto host = text.substr(0, colon);
  if (!isHostName(host)) return std::nullopt;
  if (colon == std::string::npos) return ParsedAddress{host, std::nullopt};
  auto port = parsePort(text.substr(colon + 1));
  if (!port) return std::nullopt;
  return ParsedAddress{host, port};
}

ConnectionStore::ConnectionStore(std::filesystem::path directory, std::function<void(std::string const &)> warn)
    : path_(std::move(directory) / kFileName), warn_(std::move(warn)) {}

std::optional<ConnectionInfo> ConnectionStore::load() const {
  std::error_code exists;
  if (!std::filesystem::exists(path_, exists)) return ConnectionInfo{};
  try {
    std::ifstream file(path_);
    std::stringstream text;
    text << file.rdbuf();
    auto object = json::parse(text.str());
    ConnectionInfo info;
    if (auto it = object.find("host"); it != object.end() && !it->is_null()) info.host = it->get<std::string>();
    if (auto it = object.find("port"); it != object.end() && !it->is_null()) info.port = it->get<int>();
    if (auto it = object.find("token"); it != object.end() && !it->is_null()) info.token = it->get<std::string>();
    return info;
  } catch (std::exception const &e) {
    if (warn_) warn_(std::string("connection.json could not be read: ") + e.what());
    return std::nullopt;
  }
}

void ConnectionStore::save(ConnectionInfo const &info) const {
  json object = {{"host", nullptr}, {"port", nullptr}, {"token", nullptr}};
  if (info.host) object["host"] = *info.host;
  if (info.port) object["port"] = *info.port;
  if (info.token) object["token"] = *info.token;
  writeFileAtomically(path_, object.dump(2));
}

std::string ConnectionStatus::toString() const {
  auto show = [](auto const &value) -> std::string {
    if (!value) return "-";
    if constexpr (std::is_same_v<std::decay_t<decltype(*value)>, std::string>) return *value;
    else return std::to_string(*value);
  };
  return "state=" + std::to_string(static_cast<int>(state)) + " endpoint=" + endpoint + " host=" + show(hostVersion) + " api=" + show(hostApiVersion) +
         " reason=" + show(reason) + " update=" + show(updateReason) + " db=" + show(dbVersion) + " instance=" + show(hostInstance) + " signals=" + show(signals) + " detail=" + detail;
}

HostConnection::HostConnection(HostClient &client, ConnectionStore &store, std::function<Settings()> settings, std::string defaultEnvironmentName)
    : client_(client), store_(store), settings_(std::move(settings)), defaultEnvironmentName_(std::move(defaultEnvironmentName)) {}

std::optional<std::pair<Endpoint, std::string>> HostConnection::ready() const {
  std::lock_guard lock(mutex_);
  return ready_;
}

ConnectionStatus HostConnection::refresh() {
  auto info = store_.load();
  if (!info) {
    std::lock_guard lock(mutex_);
    ready_.reset();
    return {.state = ConnectionState::LocalInfoUnreadable};
  }
  return checkHealth(info->endpoint(), info->token, settings_());
}

ConnectionStatus HostConnection::pair(std::string const &code, std::optional<ParsedAddress> const &address) {
  auto info = store_.load();
  if (!info) return {.state = ConnectionState::LocalInfoUnreadable};
  if (address) {
    // 既定と同じつなぐ先は空にしておく（既定のポートが変わっても追える）
    bool isDefault = ConnectionInfo::isLoopback(address->host) && !address->port;
    info->host = isDefault ? std::nullopt : std::optional(address->host);
    info->port = address->port;
  }
  auto settings = settings_();
  auto endpoint = info->endpoint();
  auto trimmed = code;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
  auto name = settings.environmentName.empty() ? defaultEnvironmentName_ : settings.environmentName;
  auto result = client_.pair(endpoint, {trimmed, name}, settings.hostRequestTimeout());
  if (result.kind == HostResultKind::Unreachable) return {.state = ConnectionState::Unreachable, .endpoint = endpoint.url(), .detail = result.detail};
  if (result.isVersionMismatch())
    return {.state = ConnectionState::ApiVersionMismatch, .endpoint = endpoint.url(), .hostApiVersion = result.error ? result.error->apiVersion : std::nullopt};
  if (!result.ok() || result.body->token.empty())
    return {.state = result.isUnauthorized() ? ConnectionState::PairingRejected : ConnectionState::Error, .endpoint = endpoint.url(), .detail = result.detailOrStatus()};
  info->token = result.body->token;
  store_.save(*info);
  return refresh();
}

ConnectionStatus HostConnection::checkHealth(Endpoint const &endpoint, std::optional<std::string> const &token, Settings const &settings) {
  ConnectionStatus status{.endpoint = endpoint.url()};
  std::optional<std::pair<Endpoint, std::string>> ready;
  auto finish = [&] {
    std::lock_guard lock(mutex_);
    ready_ = ready;
    return status;
  };

  auto result = client_.health(endpoint, token, settings.hostRequestTimeout());
  switch (result.kind) {
  case HostResultKind::Unreachable:
    status.state = ConnectionState::Unreachable;
    status.detail = result.detail;
    return finish();
  case HostResultKind::Malformed:
    status.state = ConnectionState::Error;
    status.detail = result.detail;
    return finish();
  case HostResultKind::Failed:
    if (result.isVersionMismatch()) {
      status.state = ConnectionState::ApiVersionMismatch;
      status.hostApiVersion = result.error ? result.error->apiVersion : std::nullopt;
    } else {
      status.state = ConnectionState::Error;
      status.detail = result.detailOrStatus();
    }
    return finish();
  case HostResultKind::Ok:
    break;
  }

  auto const &health = *result.body;
  status.hostApiVersion = health.apiVersion;
  auto hostApi = ContractVersion::parse(health.apiVersion);
  if (!hostApi) {
    status.state = ConnectionState::Error;
    status.detail = "apiVersion is malformed: " + health.apiVersion;
    return finish();
  }
  if (hostApi->major != kApiVersion.major) {
    status.state = ConnectionState::ApiVersionMismatch;
    return finish();
  }
  status.hostVersion = health.hostVersion;
  status.reason = health.reason;
  status.updateReason = health.updateReason;
  status.hostInstance = health.instanceId;
  status.signals = health.signals;
  status.presetsRevision = health.presetsRevision;
  if (health.db) {
    status.dbVersion = health.db->dbVersion;
    status.viewRoles = viewRolesOf(health.db->roles);
  }

  // トークンが有効なときだけ paired: true が返る
  if (health.paired != true) {
    status.state = ConnectionState::NeedsPairing;
    return finish();
  }
  if (health.status == health_status::kReady) {
    status.state = ConnectionState::Ready;
    ready = std::pair{endpoint, token.value_or("")};
  } else if (health.status == health_status::kLoading) {
    status.state = ConnectionState::Loading;
  } else if (health.status == health_status::kDegraded) {
    status.state = ConnectionState::Degraded;
  } else {
    status.state = ConnectionState::Error;
    status.detail = "unknown status: " + health.status;
  }
  return finish();
}

} // namespace octantpath::core
