#pragma once

#include "octantpath/core/host_client.hpp"
#include "octantpath/core/settings.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace octantpath::core {

// OctantPath の接続情報（connection.json）。つなぐ先とトークンの正本（憲法の原則 7）。
// 契約6（つなぐ側 ↔ OctantCompass）2.0 では OctantCompass のファイルを読まず、ここにあるつなぐ先へ通信だけでつなぐ
struct ConnectionInfo {
  // 同じ機器の OctantCompass が既定で待ち受けるところ（契約6 2.0）。Quest では USB の adb reverse で PC に届く
  static constexpr char const *kDefaultHost = "127.0.0.1";
  static constexpr int kDefaultPort = 23287;

  // つなぐ先のアドレスとポート。空なら既定
  std::optional<std::string> host;
  std::optional<int> port;
  std::optional<std::string> token;

  std::string effectiveHost() const { return host && !host->empty() ? *host : kDefaultHost; }
  int effectivePort() const { return port.value_or(kDefaultPort); }
  Endpoint endpoint() const { return {effectiveHost(), effectivePort()}; }
  // 人に見せるつなぐ先（parseAddress で読み戻せる形）。空なら既定
  std::string address() const;

  static bool isLoopback(std::string const &host);
};

// 人が入れたつなぐ先（"192.168.0.10"、"192.168.0.10:23287"、"[fe80::1]:23287" など）を読む。
// ポートを省けば port は空（既定のポート）。読めなければ nullopt
struct ParsedAddress {
  std::string host;
  std::optional<int> port;
};
std::optional<ParsedAddress> parseAddress(std::string const &text);

class ConnectionStore {
public:
  static constexpr char const *kFileName = "connection.json";

  ConnectionStore(std::filesystem::path directory, std::function<void(std::string const &)> warn);

  // 読めないときは nullopt（壊れたファイルを新しい中身で上書きしないため）。ファイルがなければ空の接続情報
  std::optional<ConnectionInfo> load() const;
  // 書きかけのファイルを残さないよう、別のファイルに書いてから置き換える
  void save(ConnectionInfo const &info) const;

private:
  std::filesystem::path path_;
  std::function<void(std::string const &)> warn_;
};

enum class ConnectionState {
  // まだ確かめていない
  Unknown,
  // OctantPath の接続情報が読めない
  LocalInfoUnreadable,
  // つなぐ先に届かない
  Unreachable,
  // 契約3 のメジャー版が違う
  ApiVersionMismatch,
  // ペアリングが要る（まだ合言葉を入れていない、またはトークンが取り消された）
  NeedsPairing,
  // ペアリングを断られた
  PairingRejected,
  // DB の読み込み中
  Loading,
  // 推薦が使えない（理由は reason）
  Degraded,
  // 推薦が使える
  Ready,
  // 想定外の応答
  Error,
};

struct ConnectionStatus {
  ConnectionState state = ConnectionState::Unknown;
  std::string endpoint;
  std::optional<std::string> hostApiVersion;
  std::optional<std::string> hostVersion;
  std::optional<std::string> reason;
  std::optional<std::string> updateReason;
  std::optional<std::string> dbVersion;
  // OctantCompass の起動を表す値と、そこにあるこの環境の signal の件数（ペアリング済みのときだけ）
  std::optional<std::string> hostInstance;
  std::optional<long long> signals;
  std::optional<std::string> presetsRevision;
  // ビュー（id@version）から役割名を引く表
  std::map<std::string, std::string> viewRoles;
  std::string detail;

  std::string toString() const;
};

// 契約6（つなぐ側 ↔ OctantCompass）2.0 の OctantPath 側。自分の接続情報にあるつなぐ先（なければ既定）へ通信だけでつなぎ、
// 人が入れた合言葉でペアリングし、/v1/health で状態を確かめる。どのスレッドから呼んでもよい（ゲームのスレッドからは呼ばない）
class HostConnection {
public:
  HostConnection(HostClient &client, ConnectionStore &store, std::function<Settings()> settings, std::string defaultEnvironmentName);

  ConnectionStatus refresh();

  // 人が入れた合言葉でペアリングする。address が空なら今のつなぐ先のまま。
  // 通ったら、つなぐ先とトークンを接続情報に書いて確かめ直す。通らなければ接続情報は変えない
  ConnectionStatus pair(std::string const &code, std::optional<ParsedAddress> const &address);

  // 最後に確かめたときに推薦が使えたつなぐ先とトークン。使えないときは nullopt
  std::optional<std::pair<Endpoint, std::string>> ready() const;

private:
  ConnectionStatus checkHealth(Endpoint const &endpoint, std::optional<std::string> const &token, Settings const &settings);

  HostClient &client_;
  ConnectionStore &store_;
  std::function<Settings()> settings_;
  std::string defaultEnvironmentName_;

  mutable std::mutex mutex_;
  std::optional<std::pair<Endpoint, std::string>> ready_;
};

} // namespace octantpath::core
