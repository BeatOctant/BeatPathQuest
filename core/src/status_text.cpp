#include "octantpath/core/status_text.hpp"

#include "octantpath/core/contract_version.hpp"

namespace octantpath::core {

Language resolveLanguage(std::string const &setting, std::string const &deviceLocale) {
  if (setting == "ja") return Language::Ja;
  if (setting == "en") return Language::En;
  return deviceLocale.starts_with("ja") ? Language::Ja : Language::En;
}

std::string describe(ConnectionStatus const &status, Language language) {
  bool ja = language == Language::Ja;
  auto ours = kApiVersion.str();
  switch (status.state) {
  case ConnectionState::Unknown:
    return ja ? "接続中" : "Connecting";
  case ConnectionState::LocalInfoUnreadable:
    return ja ? "OctantPath の接続情報が読めません（Mods/octantpath/connection.json）" : "Can't read OctantPath's connection info (Mods/octantpath/connection.json)";
  case ConnectionState::Unreachable:
    return ja ? "OctantCompass につながりません（OctantCompass が動いているか、つなぐ先を確かめてください）"
              : "Cannot reach OctantCompass (check that it is running and that the address is right)";
  case ConnectionState::ApiVersionMismatch: {
    auto host = status.hostApiVersion ? ContractVersion::parse(*status.hostApiVersion) : std::nullopt;
    if (host && host->major != kApiVersion.major) {
      std::string update = host->major > kApiVersion.major ? "OctantPath" : "OctantCompass";
      return ja ? "OctantCompass（" + *status.hostApiVersion + "）と OctantPath（" + ours + "）の版が合いません。" + update + " を更新してください"
                : "OctantCompass (" + *status.hostApiVersion + ") and OctantPath (" + ours + ") don't match. Update " + update;
    }
    return ja ? "OctantCompass と OctantPath（" + ours + "）の版が合いません" : "OctantCompass and OctantPath (" + ours + ") don't match";
  }
  case ConnectionState::NeedsPairing:
    return ja ? "ペアリングが必要です（OctantCompass の画面に出る合言葉を入れてください）" : "Pairing needed (enter the code OctantCompass shows)";
  case ConnectionState::PairingRejected:
    return ja ? "ペアリングを断られました（合言葉が違うか、古くなっています）" : "Pairing was refused (the passphrase is wrong or has expired)";
  case ConnectionState::Loading:
    return ja ? "準備中（OctantCompass が DB を読み込んでいます）" : "Getting ready (OctantCompass is loading the DB)";
  case ConnectionState::Degraded: {
    auto reason = status.reason.value_or(ja ? "理由不明" : "unknown reason");
    return ja ? "推薦は使えません（OctantCompass: " + reason + "）" : "Recommendations are unavailable (OctantCompass: " + reason + ")";
  }
  case ConnectionState::Ready:
    if (status.updateReason) return ja ? "接続済（DB を更新できません: " + *status.updateReason + "）" : "Connected (can't update the DB: " + *status.updateReason + ")";
    return ja ? "接続済" : "Connected";
  case ConnectionState::Error:
    break;
  }
  return ja ? "OctantCompass から想定外の応答がありました" : "OctantCompass sent an unexpected reply";
}

} // namespace octantpath::core
