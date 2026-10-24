#include "ui/octantpath_view.hpp"

#include "main.hpp"
#include "services.hpp"
#include "ui/recommend_panel.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include "HMUI/CurvedTextMeshPro.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace octantpath::ui {

namespace {

using core::Language;

struct Texts {
  char const *address;
  char const *code;
  char const *pair;
  char const *refresh;
  char const *badAddress;
  char const *working;
  char const *where;
};

Texts const &texts(Language language) {
  static Texts const ja{
      "つなぐ先", "合言葉", "ペアリング", "確かめ直す", "つなぐ先の形が読めません（例: 192.168.0.10:23287）", "確かめています…",
      "推薦は、選曲画面の上に出る板で受け取ります",
  };
  static Texts const en{
      "Address", "Code", "Pair", "Check again", "The address cannot be read (e.g. 192.168.0.10:23287)", "Checking…",
      "Get recommendations on the panel above song select",
  };
  return language == Language::Ja ? ja : en;
}

// 画面が開くたびに作り直さないので、部品と入力中の値はここに持つ。画面の部品はゲームのスレッドでだけ触る
UnityW<HMUI::CurvedTextMeshPro> statusText;

std::mutex inputMutex;
std::string addressInput;
std::string codeInput;
std::atomic<bool> busy = false;

// 設定の入れ物は子の幅は好みの幅に合わせるが、高さは決めずに今の高さのまま並べる。
// 幅は好みの幅で決め、高さは文字を変えるたびに折り返した行数に合わせて自分で与える。
// （ContentSizeFitter で伸ばすと、入れ物が並べたあとに伸びるため、最初の部品が枠の上にはみ出していた）
constexpr float kTextWidth = 90.0f;
constexpr float kStatusGap = 2.0f;

void setWrappedText(HMUI::CurvedTextMeshPro *t, std::string const &text, float bottomGap) {
  t->set_text(text);
  auto height = t->GetPreferredValues(kTextWidth, 0.0f).y;
  auto rect = t->get_rectTransform();
  rect->set_sizeDelta({kTextWidth, height + bottomGap});
  UnityEngine::UI::LayoutRebuilder::MarkLayoutForRebuild(rect);
}

UnityW<HMUI::CurvedTextMeshPro> createWrappedText(UnityEngine::Transform *parent, std::string const &text, float fontSize, float bottomGap) {
  auto t = BSML::Lite::CreateText(parent, "", TMPro::FontStyles::Normal, fontSize);
  t->set_enableWordWrapping(true);
  t->GetComponent<UnityEngine::UI::LayoutElement *>()->set_preferredWidth(kTextWidth);
  setWrappedText(t, text, bottomGap);
  return t;
}

void showStatus(std::string text) {
  BSML::MainThreadScheduler::Schedule([text = std::move(text)] {
    if (statusText) setWrappedText(statusText, text, kStatusGap);
  });
}

std::string format(core::ConnectionStatus const &status) {
  Log.info("connection: {}", status.toString());
  auto text = core::describe(status, language());
  if (!status.detail.empty() && status.state != core::ConnectionState::Ready) text += "\n<size=70%>" + status.detail + "</size>";
  if (!status.endpoint.empty()) text += "\n<size=70%>" + status.endpoint + "</size>";
  if (status.state == core::ConnectionState::Ready) text += std::string("\n<size=70%>") + texts(language()).where + "</size>";
  return text;
}

// 通信はゲームのスレッドの外で 1 つずつ行う。画面に返すのは各処理が Schedule する
void runInBackground(std::function<void()> work) {
  if (busy.exchange(true)) return;
  std::thread([work = std::move(work)] {
    try {
      work();
    } catch (std::exception const &e) {
      Log.error("background work failed: {}", e.what());
      showStatus(core::describe({.state = core::ConnectionState::Error}, language()));
    }
    busy = false;
  }).detach();
}

// 接続の状態を出し、要るなら手札の一覧を取り直して、選曲画面の板に知らせる
void checkConnection(core::ConnectionStatus status, std::string prefix = {}) {
  showStatus(prefix + format(status));
  recommender().onStatus(status);
  BSML::MainThreadScheduler::Schedule([] { refreshRecommendPanel(); });
}

void refresh() {
  showStatus(texts(language()).working);
  runInBackground([] { checkConnection(connection().refresh()); });
}

void pair() {
  std::string address, code;
  {
    std::lock_guard lock(inputMutex);
    address = addressInput;
    code = codeInput;
  }
  std::optional<core::ParsedAddress> parsed;
  if (address.find_first_not_of(" \t") != std::string::npos) {
    parsed = core::parseAddress(address);
    if (!parsed) {
      showStatus(texts(language()).badAddress);
      return;
    }
  } else {
    parsed = core::ParsedAddress{core::ConnectionInfo::kDefaultHost, std::nullopt};
  }
  showStatus(texts(language()).working);
  runInBackground([code, parsed] {
    auto status = connection().pair(code, parsed);
    if (status.state != core::ConnectionState::PairingRejected) return checkConnection(status);
    // 断られても前のペアリングは残るので、今の状態も並べる（使った合言葉をもう一度送ったときなど）
    checkConnection(connection().refresh(), format(status) + "\n\n");
  });
}

} // namespace

void octantpathViewDidActivate(HMUI::ViewController *self, bool firstActivation, bool, bool) {
  if (firstActivation) {
    auto const &t = texts(language());
    auto container = BSML::Lite::CreateScrollableSettingsContainer(self->get_transform());
    auto parent = container->get_transform();
    statusText = createWrappedText(parent, "", 4.0f, kStatusGap);

    // ペアリング（契約6）
    BSML::Lite::CreateStringSetting(parent, t.address, "", [](StringW value) {
      std::lock_guard lock(inputMutex);
      addressInput = static_cast<std::string>(value);
    });
    BSML::Lite::CreateStringSetting(parent, t.code, "", [](StringW value) {
      std::lock_guard lock(inputMutex);
      codeInput = static_cast<std::string>(value);
    });
    auto buttons = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    buttons->set_spacing(2.0f);
    BSML::Lite::CreateUIButton(buttons->get_transform(), t.pair, [] { pair(); });
    BSML::Lite::CreateUIButton(buttons->get_transform(), t.refresh, [] { refresh(); });
  }
  refresh();
}

} // namespace octantpath::ui
