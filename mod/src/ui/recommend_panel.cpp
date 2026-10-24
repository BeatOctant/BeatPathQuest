#include "ui/recommend_panel.hpp"

#include "game.hpp"
#include "levels.hpp"
#include "main.hpp"
#include "services.hpp"
#include "trials.hpp"

#include "octantpath/core/map_install.hpp"
#include "octantpath/core/panel_placement.hpp"

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "songcore/shared/SongCore.hpp"

#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/LevelCollectionNavigationController.hpp"
#include "GlobalNamespace/LevelSelectionNavigationController.hpp"
#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/Screen.hpp"
#include "HMUI/TableView.hpp"
#include "System/Object.hpp"
#include "UnityEngine/GameObject.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <span>
#include <string_view>
#include <thread>

namespace octantpath::ui {

namespace {

using core::Language;

struct Texts {
  char const *seed;
  char const *noSeed;
  char const *asking;
  char const *noCandidates;
  char const *notInstalled;
  char const *notReady;
  char const *repair;
  char const *presetsChanged;
  char const *seedNotEmbedded;
  char const *failed;
  char const *noPresets;
  char const *beatSaverFailed;
  char const *pick;
  char const *selected;
  char const *notInList;
  char const *downloading;
  char const *installed;
  char const *installFailed;
  char const *loadingPresets;
  char const *notLoaded;
};

Texts const &texts(Language language) {
  static Texts const ja{
      "起点: ", "起点: なし（曲を選ぶと、似た曲などの手札が増えます）", "推薦を頼んでいます…", "推薦がありませんでした", "未導入",
      "OctantCompass とつながっていません（メインメニューの OctantPath で確かめてください）", "ペアリングし直してください（メインメニューの OctantPath）",
      "手札が変わりました。もう一度選んでください", "推薦がありませんでした（起点の譜面は DB にありますが、比べる材料がありません）", "推薦を頼めませんでした: ",
      "手札を取れませんでした: ",
      "BeatSaver を引けませんでした（PC でプロキシを動かし、adb reverse tcp:23288 tcp:23288 をしてください）。導入済みの曲だけ出します",
      "左の手札を押すと推薦を頼みます。右の曲を押すと、導入済みは選び、未導入は取って入れます", "選びました: ", "今の一覧にない曲です。「すべて」か「カスタム」の一覧を開いてから押してください",
      "取っています: ", "入れました: ", "入れられませんでした: ", "手札を取っています…", "置きましたが、SongCore が読み込めませんでした: ",
  };
  static Texts const en{
      "Seed: ", "Seed: none (pick a song to get presets like similar songs)", "Asking for recommendations…", "No recommendations",
      "not installed", "Not connected to OctantCompass (check OctantPath in the main menu)", "Pair again (OctantPath in the main menu)",
      "The presets changed. Pick one again", "No recommendations (the seed chart is in the DB but has nothing to compare)", "Could not get recommendations: ",
      "Could not get the presets: ",
      "Could not reach BeatSaver (run the proxy on the PC and adb reverse tcp:23288 tcp:23288). Showing installed songs only",
      "Press a preset on the left to get recommendations. Press a song on the right to select it, or to download it if not installed", "Selected: ", "The song is not in the current list. Open All or Custom first",
      "Downloading: ", "Installed: ", "Could not install: ", "Getting the presets…", "Placed, but SongCore did not load it: ",
  };
  return language == Language::Ja ? ja : en;
}

// 板の大きさ（UI の単位）。拡大率は BSML の FloatingScreen が付ける 0.02。左に手札の一覧、右に推薦の一覧を並べる。
// 決まった位置は、曲選択の画面の上の段（戻るボタンや並べ替えのタブ）にかからない高さに、少し手前へ傾けて置く。
// 上の縁の取っ手でつかんで動かせ、放した位置を覚えておく
constexpr float kPanelWidth = 150.0f;
constexpr float kPanelHeight = 96.0f;
constexpr float kPanelScale = 0.02f;
constexpr float kGap = 0.35f;
constexpr float kTilt = 20.0f;
constexpr float kPadding = 3.0f;
constexpr float kWidth = kPanelWidth - 2 * kPadding;
constexpr float kListGap = 2.0f;
constexpr float kPresetListWidth = 56.0f;
constexpr float kListHeight = 74.0f;

// 板の部品と、今出している推薦。ゲームのスレッドでだけ触る
UnityW<BSML::FloatingScreen> panel;
UnityW<HMUI::CurvedTextMeshPro> seedText;
UnityW<HMUI::CurvedTextMeshPro> noticeText;
UnityW<BSML::CustomListTableData> presetList;
UnityW<BSML::CustomListTableData> list;
UnityW<GlobalNamespace::LevelCollectionNavigationController> collection;

struct PresetEntry {
  std::string label;
  core::Preset preset;
  std::map<std::string, std::string> choices;
};
// 手札の一覧の行と同じ順
std::vector<PresetEntry> presetEntries;
// 今の手札の一覧を作ったときの一覧の revision と、起点があったか（変わったら作り直す）
std::optional<std::pair<std::string, bool>> shownPresets;

struct Row {
  core::RecommendationItem item;
  bool installed = false;
  // 推薦の要求 id と手札の id（入れたときの所有の記録に書く）
  std::string requestId;
  std::string presetId;
};
// 一覧の行と同じ順
std::vector<Row> rows;
// カバーは hash ごとに 1 度だけ取る
std::map<std::string, UnityW<UnityEngine::Sprite>> covers;
// 一覧を出し直すたびに増やす。古い一覧のカバーを新しい一覧に付けないため
int listGeneration = 0;

std::atomic<bool> busy = false;

void setText(HMUI::CurvedTextMeshPro *t, std::string const &text) {
  if (t) t->set_text(text);
}

void showNotice(std::string text) {
  BSML::MainThreadScheduler::Schedule([text = std::move(text)] { setText(noticeText, text); });
}

// 通信はゲームのスレッドの外で 1 つずつ行う。画面に返すのは各処理が Schedule する
bool runInBackground(std::function<void()> work) {
  if (busy.exchange(true)) return false;
  std::thread([work = std::move(work)] {
    try {
      work();
    } catch (std::exception const &e) {
      Log.error("panel work failed: {}", e.what());
      showNotice(core::describe({.state = core::ConnectionState::Error}, language()));
    }
    busy = false;
  }).detach();
  return true;
}

std::string number(double value) {
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "%.1f", value);
  return buffer;
}

std::string join(std::vector<std::string> const &items, char const *separator) {
  std::string text;
  for (auto const &item : items) text += (text.empty() ? "" : separator) + item;
  return text;
}

// 行の上の段（曲名と作者）と下の段（マッパー、難易度、NPS、未導入か）
std::pair<std::string, std::string> rowTexts(Row const &row) {
  auto const &item = row.item;
  std::string title, author, mappers;
  if (item.map) {
    auto const &m = item.map->metadata;
    title = m.songName.empty() ? item.map->name : m.songName;
    author = m.songAuthorName;
    mappers = m.levelAuthorName;
  } else if (auto level = installedLevel(item.hash)) {
    title = level->songName;
    author = level->songAuthorName;
    mappers = join(level->mappers, ", ");
  }
  if (!author.empty()) title += " <size=80%><color=#bbbbbb>- " + author + "</color></size>";
  std::vector<std::string> details;
  if (!mappers.empty()) details.push_back(mappers);
  details.push_back(item.candidate.difficulty + (item.candidate.characteristic == "Standard" ? "" : " (" + item.candidate.characteristic + ")"));
  if (item.nps) details.push_back(number(*item.nps) + " NPS");
  // 下の段は、ゲームの曲の行が色の指定を読まないので素の文字にする
  if (!row.installed) details.push_back(texts(language()).notInstalled);
  return {title, join(details, "  ")};
}

void showRows() {
  if (!list) return;
  auto data = ListW<BSML::CustomCellInfo *>::New();
  for (auto const &row : rows) {
    auto [title, details] = rowTexts(row);
    auto cover = covers.find(row.item.hash);
    data->Add(BSML::CustomCellInfo::construct(title, details, cover != covers.end() && cover->second ? cover->second.ptr() : nullptr));
  }
  list->data = data;
  list->tableView->ReloadData();
}

// 一覧の行のカバーを、PC のプロキシ越しに 1 枚ずつ取って付ける
void loadCovers(int generation, std::vector<std::pair<std::string, std::string>> wanted) {
  std::thread([generation, wanted = std::move(wanted)] {
    for (auto const &[hash, url] : wanted) {
      std::string error;
      auto bytes = beatSaver().download(url, &error);
      if (!bytes) {
        Log.warn("cover {} failed: {}", hash, error);
        continue;
      }
      BSML::MainThreadScheduler::Schedule([generation, hash, bytes = std::move(*bytes)] {
        ArrayW<uint8_t> array(bytes.size());
        std::memcpy(array.begin(), bytes.data(), bytes.size());
        auto sprite = BSML::Lite::ArrayToSprite(array);
        if (!sprite) return;
        covers[hash] = sprite;
        if (generation != listGeneration || !list) return;
        for (size_t i = 0; i < rows.size() && i < static_cast<size_t>(list->data->get_Count()); ++i) {
          if (rows[i].item.hash == hash) list->data[i]->icon = sprite;
        }
        list->tableView->RefreshCellsContent();
      });
    }
  }).detach();
}

// 出す候補を選ぶ。BeatSaver に推薦された版があるものだけを、OctantCompass の順のまま mixDraftCount 件まで（PC 版と同じ）。
// BeatSaver を引けなかったときは、導入済みの曲だけを出す
void showCandidates(std::vector<core::RecommendationItem> const &items, std::string const &beatSaverError, int count, std::string const &requestId, std::string const &presetId) {
  rows.clear();
  for (auto const &item : items) {
    if (static_cast<int>(rows.size()) >= count) break;
    bool installed = !item.hash.empty() && installedLevel(item.hash).has_value();
    if (!(item.shown() || (!beatSaverError.empty() && installed))) continue;
    rows.push_back({item, installed, requestId, presetId});
  }
  ++listGeneration;
  showRows();
  auto const &t = texts(language());
  if (rows.empty()) setText(noticeText, t.noCandidates);
  else if (!beatSaverError.empty()) setText(noticeText, std::string("<color=#ffaa66>") + t.beatSaverFailed + "</color>");
  else setText(noticeText, t.pick);

  std::vector<std::pair<std::string, std::string>> wanted;
  for (auto const &row : rows) {
    if (row.item.version && !row.item.version->coverUrl.empty() && !covers.contains(row.item.hash)) wanted.emplace_back(row.item.hash, row.item.version->coverUrl);
  }
  if (!wanted.empty()) loadCovers(listGeneration, std::move(wanted));
}

std::string failureText(core::HostResult<core::RecommendationResponse> const &result) {
  auto const &t = texts(language());
  if (result.isUnauthorized()) return t.repair;
  if (result.isUnknownPreset()) return t.presetsChanged;
  if (result.error && result.error->code == "seed_not_embedded") return t.seedNotEmbedded;
  return t.failed + result.detailOrStatus();
}

void recommend(core::Preset preset, std::map<std::string, std::string> choices) {
  auto seed = selectedLevel();
  auto started = runInBackground([preset = std::move(preset), choices = std::move(choices), seed] {
    auto result = recommender().recommend(preset, seed ? std::optional(seed->chart) : std::nullopt, choices);
    if (!result) {
      // 手札の一覧がまだない、つながっていないなど。つなぎ直して手札を取り直す
      auto status = connection().refresh();
      recommender().onStatus(status);
      showNotice(texts(language()).notReady);
      BSML::MainThreadScheduler::Schedule([] { refreshRecommendPanel(); });
      return;
    }
    if (!result->ok()) {
      Log.warn("recommend {} failed: {}", preset.id, result->detailOrStatus());
      showNotice(failureText(*result));
      if (result->shouldRecheck() || result->isUnknownPreset()) {
        recommender().onStatus(connection().refresh());
        BSML::MainThreadScheduler::Schedule([] { refreshRecommendPanel(); });
      }
      return;
    }
    auto response = *result->body;
    Log.info("recommend {}: {} candidates ({})", preset.id, response.candidates.size(), response.requestId);
    std::vector<std::string> hashes;
    for (auto const &candidate : response.candidates) hashes.push_back(candidate.hash);
    auto lookup = beatSaver().mapsByHash(hashes);
    if (!lookup.error.empty()) Log.warn("BeatSaver lookup failed: {}", lookup.error);
    auto items = core::recommendationItems(response, lookup);
    auto count = std::max(1, settings().mixDraftCount);
    BSML::MainThreadScheduler::Schedule([items = std::move(items), error = lookup.error, count, requestId = response.requestId, presetId = preset.id] {
      showCandidates(items, error, count, requestId, presetId);
    });
  });
  if (started) setText(noticeText, texts(language()).asking);
}

// 手札の行を押した。その手札で推薦を頼む
void presetClicked(int index) {
  if (presetList) presetList->tableView->ClearSelection();
  if (index < 0 || static_cast<size_t>(index) >= presetEntries.size()) return;
  auto const &entry = presetEntries[index];
  recommend(entry.preset, entry.choices);
}

// 導入済みの曲を、今の一覧にあれば選曲画面で選ぶ
// 入れたばかりの曲は、SongCore が読み直しの後始末で、読み直す前に選ばれていた曲を少しあとに選び直す（NavigationControllerUpdater）。
// そのため、選んだあともしばらく見張り、戻されたら選び直す
constexpr int kSelectChecks = 6;
constexpr float kSelectCheckSeconds = 0.25f;

void keepSelected(std::string hash, int check) {
  // プレイが始まったら選び直さない（OctantPath 法 2.6）
  if (inGameplay()) return;
  auto const &t = texts(language());
  auto level = SongCore::API::Loading::GetLevelByHash(hash);
  if (!level || !collection) {
    Log.info("select {}: check {}, {}", hash, check, level ? "no song list" : "SongCore does not have it");
  } else if (collection->beatmapLevel != level) {
    collection->SelectLevel(level);
    Log.info("select {}: check {}, {}", hash, check, collection->beatmapLevel == level ? "selected" : "not in the current list");
  }
  bool last = check + 1 >= kSelectChecks;
  if (last) {
    bool selected = level && collection && collection->beatmapLevel == level;
    Log.info("select {}: {}", hash, selected ? "done" : "gave up");
    return setText(noticeText, selected ? t.selected + static_cast<std::string>(level->songName) : std::string(t.notInList));
  }
  BSML::MainThreadScheduler::ScheduleAfterTime(kSelectCheckSeconds, [hash = std::move(hash), check] { keepSelected(hash, check + 1); });
}

// 導入済みの曲を、今の一覧にあれば選曲画面で選ぶ。justInstalled なら、SongCore に戻されないよう、しばらく見張る
void selectInstalled(std::string const &hash, bool justInstalled = false) { keepSelected(hash, justInstalled ? 0 : kSelectChecks - 1); }

std::string installFailureText(core::InstallFailure failure, Language language) {
  using F = core::InstallFailure;
  bool ja = language == Language::Ja;
  switch (failure) {
  case F::NameTaken: return ja ? "同じ名前のフォルダがあります" : "a folder with the same name exists";
  case F::HashMismatch: return ja ? "推薦された版と中身が違います" : "the contents differ from the recommended version";
  case F::ZipTooLarge: case F::ExtractedTooLarge: return ja ? "大きすぎます" : "too large";
  case F::TooManyEntries: return ja ? "ファイルが多すぎます" : "too many files";
  case F::BadEntryName: case F::InfoPointsOutside: return ja ? "フォルダの外を指す名前があります" : "a name points outside the folder";
  case F::NoInfo: case F::ManyRoots: case F::InfoUnreadable: case F::InfoMalformed: case F::InfoFileMissing: return ja ? "マップの形が違います" : "the map is malformed";
  case F::ZipUnreadable: return ja ? "zip が読めません" : "the zip cannot be read";
  case F::Io: return ja ? "書き込めませんでした" : "could not write";
  default: return ja ? "入れられません" : "cannot install";
  }
}

// 未導入の曲を、PC のプロキシ越しに取って確かめ、SongCore の曲の置き場所に入れる。入ったら読み直させて、選曲画面で選ぶ
void installRow(Row const &row) {
  auto const &t = texts(language());
  auto const &item = row.item;
  if (!item.map || !item.version) return setText(noticeText, std::string(t.installFailed) + "BeatSaver");
  auto title = item.map->metadata.songName.empty() ? item.map->name : item.map->metadata.songName;
  auto folder = core::folderName(item.map->key, title, item.map->metadata.levelAuthorName);
  auto levels = SongCore::API::Loading::GetPreferredCustomLevelPath();
  // 所有の記録の中身。取った時刻は入れ終わったときに付ける
  core::OwnedMap owned{item.hash, item.map->key, folder};
  owned.frames.push_back({core::FrameEntry::kManual, row.requestId, row.presetId, item.candidate.characteristic, item.candidate.difficulty});
  auto started = runInBackground([item, title, folder, levels, owned]() mutable {
    auto const &t = texts(language());
    std::string error;
    auto zip = beatSaver().mapZip(item.hash, &*item.version, &error);
    if (!zip) {
      Log.warn("download {} failed: {}", item.hash, error);
      return showNotice(t.installFailed + title + "（" + error + "）");
    }
    // 取っているあいだにプレイが始まったら、展開・照合・設置はメニューに戻るまで待つ（OctantPath 法 2.6）
    waitUntilNotPlaying();
    auto result = core::installMap(item.hash, *zip, folder, levels, dataDirectory() / "work", settings());
    if (!result.ok()) {
      Log.warn("install {} failed: {} {}", item.hash, static_cast<int>(result.failure), result.detail);
      return showNotice(t.installFailed + title + "（" + installFailureText(result.failure, language()) + "）");
    }
    Log.info("installed {} to {}", item.hash, result.path.string());
    owned.acquiredAt = core::formatUtc(std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()));
    owned.frames[0].at = owned.acquiredAt;
    // 読み直しは、プレイ中ならメニューに戻ってから（OctantPath 法 2.6）
    BSML::MainThreadScheduler::Schedule([hash = item.hash, title, owned] {
      refreshSongsWhenIdle([hash, title, owned] {
        // 所有は、検証と設置が済み、SongCore が読み込んだあとにだけ記録する（OctantPath 法 3.1）。読み込めなければ取得済みとして扱わない（2.5）
        bool loaded = installedLevel(hash).has_value();
        if (loaded) recordInstalled(owned);
        for (auto &row : rows) {
          if (row.item.hash == hash) row.installed = loaded;
        }
        showRows();
        auto const &t = texts(language());
        setText(noticeText, (loaded ? t.installed : t.notLoaded) + title);
        if (loaded) selectInstalled(hash, true);
      });
    });
  });
  if (started) setText(noticeText, t.downloading + title);
}

// 行を押した。導入済みの曲は選曲画面で選び、未導入の曲は取って入れる
void rowClicked(int index) {
  if (list) list->tableView->ClearSelection();
  if (index < 0 || static_cast<size_t>(index) >= rows.size()) return;
  auto const &row = rows[index];
  if (row.installed) return selectInstalled(row.item.hash);
  installRow(row);
}

// 手札の行は 1 行に収める。行は折り返しを強いるので、幅（全角を 2 と数える）で切って、折り返さないよう囲む
std::string fitLabel(std::string const &label) {
  constexpr int kMaxWidth = 30;
  int width = 0;
  size_t i = 0;
  while (i < label.size()) {
    auto c = static_cast<unsigned char>(label[i]);
    size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    int charWidth = c < 0x80 ? 1 : 2;
    if (width + charWidth > kMaxWidth) return "<nobr>" + label.substr(0, i) + "…</nobr>";
    width += charWidth;
    i += length;
  }
  return "<nobr>" + label + "</nobr>";
}

void showPresetRows() {
  if (!presetList) return;
  auto data = ListW<BSML::CustomCellInfo *>::New();
  for (auto const &entry : presetEntries) data->Add(BSML::CustomCellInfo::construct(fitLabel(entry.label), "", nullptr));
  presetList->data = data;
  presetList->tableView->ReloadData();
}

// 部品の好みの大きさ。LayoutElement がなければ足す
UnityEngine::UI::LayoutElement *layoutOf(UnityEngine::Component *component) {
  auto layout = component->GetComponent<UnityEngine::UI::LayoutElement *>();
  return layout ? layout : component->get_gameObject()->AddComponent<UnityEngine::UI::LayoutElement *>();
}

UnityW<HMUI::CurvedTextMeshPro> createText(UnityEngine::Transform *parent, float fontSize, float height) {
  auto text = BSML::Lite::CreateText(parent, "", TMPro::FontStyles::Normal, fontSize);
  text->set_enableWordWrapping(true);
  text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
  auto layout = layoutOf(text);
  layout->set_preferredWidth(kWidth);
  layout->set_preferredHeight(height);
  return text;
}

// 並べる入れ物。BSML の vertical・horizontal は中身の大きさに縮む ContentSizeFitter を持ち、
// 板いっぱいに広げても縮んで中心からずれるので、使わずに自分で作る
template <class Group> Group *createGroup(UnityEngine::Transform *parent, char const *name) {
  auto gameObject = UnityEngine::GameObject::New_ctor(name);
  gameObject->get_transform()->SetParent(parent, false);
  auto group = gameObject->AddComponent<Group *>();
  group->set_childControlWidth(true);
  group->set_childControlHeight(true);
  group->set_childForceExpandWidth(false);
  group->set_childForceExpandHeight(false);
  return group;
}

void logSize(char const *name, UnityEngine::Component *component) {
  if (!component) return;
  auto rect = component->get_transform().cast<UnityEngine::RectTransform>();
  auto r = rect->get_rect();
  auto s = rect->get_lossyScale();
  Log.info("panel layout {}: {:.1f} x {:.1f} at ({:.1f}, {:.1f}), scale {:.4f}", name, r.get_width(), r.get_height(), r.get_x(), r.get_y(), s.x);
}

core::PanelPlacementStore const &placements() {
  static core::PanelPlacementStore store(dataDirectory(), [](std::string const &message) { Log.warn("{}", message); });
  return store;
}

void panelReleased(BSML::FloatingScreen *, BSML::FloatingScreenHandleEventArgs const &args) {
  auto p = args.position;
  auto r = args.rotation;
  try {
    placements().save({{p.x, p.y, p.z}, {r.x, r.y, r.z, r.w}});
    Log.info("panel moved to ({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z);
  } catch (std::exception const &e) {
    Log.warn("could not save the panel placement: {}", e.what());
  }
}

// 板を、覚えている位置か、曲選択の画面（self の画面）の上に作る
void createPanel(HMUI::ViewController *self) {
  auto screen = self->get_screen();
  UnityEngine::RectTransform *rect = screen ? screen->get_transform().cast<UnityEngine::RectTransform>().ptr() : self->get_rectTransform().ptr();
  auto scale = rect->get_lossyScale().y;
  auto up = rect->get_up();
  auto lift = rect->get_rect().get_height() * scale / 2.0f + kPanelHeight * kPanelScale / 2.0f + kGap;
  auto center = rect->get_position();
  UnityEngine::Vector3 position{center.x + up.x * lift, center.y + up.y * lift, center.z + up.z * lift};
  auto rotation = UnityEngine::Quaternion::op_Multiply(rect->get_rotation(), UnityEngine::Quaternion::Euler(-kTilt, 0.0f, 0.0f));

  if (auto saved = placements().load()) {
    position = {saved->position[0], saved->position[1], saved->position[2]};
    rotation = {saved->rotation[0], saved->rotation[1], saved->rotation[2], saved->rotation[3]};
  }

  panel = BSML::Lite::CreateFloatingScreen({kPanelWidth, kPanelHeight}, position, {0.0f, 0.0f, 0.0f}, 0.0f, true, true, BSML::Side::Top);
  panel->get_transform()->set_rotation(rotation);
  panel->HandleReleased.addCallback(&panelReleased);
  panel->get_gameObject()->set_name("OctantPathRecommendations");

  auto box = createGroup<UnityEngine::UI::VerticalLayoutGroup>(panel->get_transform(), "OctantPathPanelBox");
  auto boxRect = box->get_rectTransform();
  boxRect->set_anchorMin({0.0f, 0.0f});
  boxRect->set_anchorMax({1.0f, 1.0f});
  boxRect->set_offsetMin({0.0f, 0.0f});
  boxRect->set_offsetMax({0.0f, 0.0f});
  box->set_padding(UnityEngine::RectOffset::New_ctor(static_cast<int>(kPadding), static_cast<int>(kPadding), 2, 2));
  box->set_spacing(1.0f);
  auto parent = box->get_transform();

  seedText = createText(parent, 4.0f, 6.0f);
  noticeText = createText(parent, 3.4f, 9.0f);
  auto lists = createGroup<UnityEngine::UI::HorizontalLayoutGroup>(parent, "OctantPathPanelLists");
  lists->set_spacing(kListGap);
  auto listsLayout = layoutOf(lists);
  listsLayout->set_preferredWidth(kWidth);
  listsLayout->set_preferredHeight(kListHeight);
  presetList = BSML::Lite::CreateList(lists->get_transform(), {kPresetListWidth, kListHeight}, [](int index) { presetClicked(index); });
  presetList->set_listStyle(BSML::CustomListTableData::ListStyle::Simple);
  list = BSML::Lite::CreateList(lists->get_transform(), {kWidth - kPresetListWidth - kListGap, kListHeight}, [](int index) { rowClicked(index); });

  // 次の試験で大きさの食い違いを数値で追えるよう、並べ終わった後の大きさを残す
  BSML::MainThreadScheduler::Schedule([] {
    if (!panel) return;
    logSize("panel", panel.ptr());
    logSize("seed", seedText.ptr());
    logSize("notice", noticeText.ptr());
    logSize("presets", presetList.ptr());
    logSize("candidates", list.ptr());
  });
}

// 手札の一覧を作り直す。choice の入力がある手札は、選択肢ごとに 1 行にする
void rebuildPresets() {
  if (!presetList) return;
  auto const &t = texts(language());
  auto seed = selectedLevel();
  setText(seedText, seed ? t.seed + seed->songName + "  <size=80%>" + *seed->chart.difficulty + "</size>" : std::string(t.noSeed));
  auto presets = recommender().presets();
  std::optional<std::pair<std::string, bool>> key;
  if (presets) key = std::pair(presets->revision, seed.has_value());
  if (presets && key == shownPresets) return;
  shownPresets = key;

  presetEntries.clear();
  if (presets) {
    for (auto const &preset : core::preset_rules::shown(presets->presets, seed.has_value())) {
      auto choices = core::preset_rules::choices(preset);
      if (choices.empty()) {
        presetEntries.push_back({preset.label, preset, {}});
        continue;
      }
      // 選ばせる入力が 1 つのときだけ、選択肢を行にする（2 つ以上の手札は今は出さない）
      if (choices.size() != 1) continue;
      // 切れても見分けられるよう、選択肢を先に書く
      for (auto const &option : choices[0].options) presetEntries.push_back({option.label + " - " + preset.label, preset, {{choices[0].name, option.value}}});
    }
    Log.info("panel presets: {} rows (revision {})", presetEntries.size(), presets->revision);
    if (rows.empty()) setText(noticeText, t.pick);
  } else if (auto failure = recommender().presetsFailure(); !failure.empty()) {
    Log.warn("panel presets failed: {}", failure);
    setText(noticeText, t.noPresets + failure);
  } else {
    setText(noticeText, t.loadingPresets);
  }
  showPresetRows();
}

void showPanel(GlobalNamespace::LevelSelectionNavigationController *self) {
  collection = self->_levelCollectionNavigationController;
  if (!panel) createPanel(self);
  panel->get_gameObject()->SetActive(true);
  rebuildPresets();
  // 手札の一覧がまだなければ、つないで取る
  if (!recommender().presets()) {
    runInBackground([] {
      recommender().onStatus(connection().refresh());
      BSML::MainThreadScheduler::Schedule([] { refreshRecommendPanel(); });
    });
  }
}

} // namespace

MAKE_HOOK_MATCH(LevelSelectionDidActivate, &GlobalNamespace::LevelSelectionNavigationController::DidActivate, void, GlobalNamespace::LevelSelectionNavigationController *self,
                bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
  LevelSelectionDidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);
  try {
    showPanel(self);
  } catch (std::exception const &e) {
    Log.error("could not show the panel: {}", e.what());
  }
}

MAKE_HOOK_MATCH(LevelSelectionDidDeactivate, &GlobalNamespace::LevelSelectionNavigationController::DidDeactivate, void, GlobalNamespace::LevelSelectionNavigationController *self,
                bool removedFromHierarchy, bool screenSystemDisabling) {
  LevelSelectionDidDeactivate(self, removedFromHierarchy, screenSystemDisabling);
  if (panel) panel->get_gameObject()->SetActive(false);
}

void installRecommendPanel() {
  INSTALL_HOOK(Log, LevelSelectionDidActivate);
  INSTALL_HOOK(Log, LevelSelectionDidDeactivate);
}

void refreshRecommendPanel() {
  if (panel) rebuildPresets();
}

} // namespace octantpath::ui
