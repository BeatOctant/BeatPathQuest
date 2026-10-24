# OctantPathAndroid

> **To use BeatOctant** (install, known issues): [Getting Started](https://github.com/BeatOctant/OctantSpecs/blob/main/GettingStarted_en.md) · [はじめかた（日本語）](https://github.com/BeatOctant/OctantSpecs/blob/main/GettingStarted_ja.md)
>
> BeatOctant is written mostly with AI coding agents (Anthropic's Claude). The maintainer decides what to build and reviews every change. It does not generate maps; it recommends existing maps from BeatSaver. ([作り方（AI の使い方）](https://github.com/BeatOctant/OctantSpecs/blob/main/README.md#作り方ai-の使い方))
>
> OctantPath for Quest: a Beat Saber Quest mod for Meta Quest (Android, arm64). It is the Quest version of [OctantPath](https://github.com/BeatOctant/OctantPath), the Beat Saber mod that recommends BeatSaver maps from your own play history.
>
> BeatOctant is an unofficial fan project for Beat Saber. It is not affiliated with, endorsed by, or sponsored by Beat Games or Meta. Beat Saber and Meta Quest are trademarks of their respective owners. Beat Games・Meta とは関係のない非公式の MOD・ツールです。
>
> 報告と案は Discord の BeatOctant サーバー（バグは `#bug-reports`、案は `#feedback-ideas`）で受けます。外からの pull request は受けていません。

> **極めて初期で不安定です。** できることは限られ、更新で壊れることがあります。ビルド済みの配布はまだなく、Getting Started の手順にも入っていません。試すときは、下の「MOD のビルド」のとおりに自分でビルドしてください。

Quest 版 Beat Saber（1.37.0）用の OctantPath の MOD（OctantPath for Quest）。[OctantCompass](https://github.com/BeatOctant/OctantCompass) の推薦をゲームの中で受け取る。PC 版の [OctantPath](https://github.com/BeatOctant/OctantPath) と同じく、[契約3（OctantPath ↔ OctantCompass）](https://github.com/BeatOctant/OctantSpecs/blob/main/contracts/mod-api.md)と[契約6（つなぐ側 ↔ OctantCompass）](https://github.com/BeatOctant/OctantSpecs/blob/main/contracts/launch.md)だけで OctantCompass とつながる。仕様はすべて [OctantSpecs](https://github.com/BeatOctant/OctantSpecs) にあり、BeatOctant 全体の中での位置は [OctantSpecs の概要](https://github.com/BeatOctant/OctantSpecs#概要)にある。

| 場所 | 中身 |
|---|---|
| `core/` | コア。契約3・6 の通信、接続の状態、設定と接続情報。ゲームにも Quest にも依存しない C++20 で、PC でビルドしてテストできる |
| `mod/` | GUI と、ゲームとのつなぎ。BSML の画面、Quest での置き場所。コアを中に入れて 1 つの `.so` になる |
| `proxy/` | PC で動くプロキシ。ネットにつながない Quest の代わりに BeatSaver を https で引く（Python 3 の標準ライブラリだけ） |
| `spikes/` | 試作。本線は依存しない。公開の写しには含めない |

いまできること: メインメニューの「OctantPath」で、OctantCompass とペアリングし、接続の状態を見る。選曲画面の上に出る板（取っ手でつかんで動かせる）の左の一覧で手札を押して推薦を受け取り、右のカバー付きの一覧で見る（曲の情報は PC のプロキシ越しに BeatSaver から）。右の行を押すと、導入済みの曲は選曲画面で選び、未導入の曲は PC のプロキシ越しに取って hash を確かめてから SongCore の曲の置き場所に入れる（取っているあいだにプレイを始めたら、展開と SongCore の読み直しはメニューに戻ってから）。入れた曲は所有の記録に書き、起動するたびに、期限と合計の上限に沿って、遊んでいないものを片付ける（既定は試し運転）。

## 組み合わせる版

支えるのは、各部品の最新の版どうしの組み合わせだけである（[decisions.md の全体と段階](https://github.com/BeatOctant/OctantSpecs/blob/main/decisions.md#全体と段階)、2026-10-22）。この README を書いた時点の相手は次のとおり。

| 相手 | 版 | つなぐもの |
| --- | --- | --- |
| Beat Saber（Quest） | 1.37.0 | 依存の MOD（beatsaber-hook、BSML、SongCore など。`mod/qpm.json`） |
| OctantCompass | 最新 | [契約3（OctantPath ↔ OctantCompass）](https://github.com/BeatOctant/OctantSpecs/blob/main/contracts/mod-api.md) 2.0、[契約6（つなぐ側 ↔ OctantCompass）](https://github.com/BeatOctant/OctantSpecs/blob/main/contracts/launch.md) 2.0 |
| OctantSpecs | `OCTANTSPECS_REF`（`main`） | コアのテストが読む契約3 の例 |

このリポジトリにはまだ版のタグがない。

## コアのテスト

```bash
scripts/fetch-octantspecs.sh   # OCTANTSPECS_REF の版の OctantSpecs（契約3 の例）を .octantspecs/ に取る
cmake -S core -B build/core -DOCTANTPATH_CORE_TESTS=ON
cmake --build build/core
build/core/octantpath-core-tests
```

## MOD のビルド

ビルドと Quest への置き方は [mod/README.md](mod/README.md) にある。

## CI

プルリクエストと main への push で、コアのテスト、プロキシのテスト、MOD のビルドを回す。手で起動したとき（workflow_dispatch）は、MOD の `.so` と `.qmod` を実行結果の Artifacts から 3 日のあいだ取れる。`v` で始まるタグを打つと、3 つが通ったあとで `.so` と `.qmod` を入れた GitHub Release の下書きができる。

## ライセンス

[MIT](LICENSE)（2026-10-21。OctantPath と同じ）。`core/third_party/` に同梱したもの（nlohmann/json、miniz、doctest。主に MIT で、一部に Apache-2.0、Unlicense、Boost Software License 1.0 の部分を含む）は、それぞれのライセンスに従う（[一覧](core/third_party/README.md)）。`.qmod` には、依存の beatsaber-hook（MIT）の `.so` も入る。`.qmod` に入るものの表記は [mod/NOTICE.txt](mod/NOTICE.txt) にある。
