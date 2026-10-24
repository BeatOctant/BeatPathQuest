#include "services.hpp"

#include "main.hpp"

#include "scotland2/shared/modloader.h"

#include <sys/system_properties.h>

namespace octantpath {

namespace {

std::string property(char const *name) {
  char value[PROP_VALUE_MAX] = {};
  __system_property_get(name, value);
  return value;
}

void warn(std::string const &message) { Log.warn("{}", message); }

} // namespace

// /sdcard/ModData/<ゲームの id>/Mods/<MOD の id>（beatsaber-hook 5 には getDataDir がないので、modloader の id から組む）
std::filesystem::path dataDirectory() { return std::filesystem::path("/sdcard/ModData") / modloader::get_application_id() / "Mods" / MOD_ID; }

core::Settings settings() { return core::loadSettings(dataDirectory(), warn); }

core::Language language() { return core::resolveLanguage(settings().language, deviceLocale()); }

std::string deviceModel() { return property("ro.product.model"); }

std::string deviceLocale() {
  auto locale = property("persist.sys.locale");
  return locale.empty() ? property("ro.product.locale") : locale;
}

static core::PosixTransport &transport() {
  static core::PosixTransport transport;
  return transport;
}

static core::HostClient &client() {
  static core::HostClient client(transport());
  return client;
}

core::HostConnection &connection() {
  static core::ConnectionStore store(dataDirectory(), warn);
  static core::HostConnection connection(client(), store, settings, "Beat Saber (" + (deviceModel().empty() ? std::string("Quest") : deviceModel()) + ")");
  return connection;
}

core::Recommender &recommender() {
  static core::Recommender recommender(client(), connection(), settings, [] { return std::string(language() == core::Language::Ja ? "ja" : "en"); });
  return recommender;
}

core::BeatSaverClient &beatSaver() {
  static core::BeatSaverClient beatSaver(transport(), settings);
  return beatSaver;
}

} // namespace octantpath
