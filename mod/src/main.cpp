#include "main.hpp"

#include "services.hpp"
#include "levels.hpp"
#include "trials.hpp"
#include "ui/octantpath_view.hpp"
#include "ui/recommend_panel.hpp"

#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "bsml/shared/BSML.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// Called at the early stages of game loading
MOD_EXTERN_FUNC void setup(CModInfo *info) noexcept {
  *info = modInfo.to_c();
  Paper::Logger::RegisterFileContextId(Log.tag);
  Log.info("OctantPath {} setup", VERSION);
}

// Called later on in the game loading, on the game's thread
MOD_EXTERN_FUNC void late_load() noexcept {
  il2cpp_functions::Init();
  BSML::Register::RegisterMainMenuViewControllerMethod("OctantPath", "OctantPath", "OctantCompass", octantpath::ui::octantpathViewDidActivate);
  octantpath::watchSelectedLevel(octantpath::ui::refreshRecommendPanel);
  octantpath::ui::installRecommendPanel();
  octantpath::installTrials();
  Log.info("OctantPath {} loaded; data in {}", VERSION, octantpath::dataDirectory().string());
}
