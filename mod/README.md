# mod

Quest の Beat Saber（1.37.0）に入れる OctantPath の MOD。画面（BSML）とゲームとのつなぎだけを持ち、通信や設定は `../core` を中に入れて使う。

いまできることは、リポジトリの [README](../README.md) にある。

## 作り方

要るもの: qpm（QPM.CLI）、NDK 27（`qpm ndk download 27`）、cmake、ninja。

1. `ndkpath.txt` に NDK の場所を 1 行で書く（`qpm ndk path 27` で分かる）。
2. `scripts/build.sh`

`build/liboctantpath.so` と `OctantPath.qmod` ができる。

## 置き方

前提の MOD（beatsaber-hook 5.1.9、paperlog、custom-types、BSML、SongCore 1.1.15）は Quest にもう入っている前提。置くのは .so だけでよい。

```bash
adb push build/liboctantpath.so /sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/
```

そのあとゲームを再起動する。

## OctantCompass につなぐ

- USB でつなぐとき: PC で `scripts/adb-reverse.sh` を実行し（OctantCompass の 23287 とプロキシの 23288 を Quest に届ける）、つなぐ先を空のままペアリングする。Quest に Wi-Fi は要らない。adb reverse は Quest の再起動や USB の抜き差しで消えるので、つなぎ直すたびに実行する。
- 家の LAN でつなぐとき: OctantCompass の待ち受けるアドレスを広げ（契約3 の共通ルール）、OctantCompass の画面に出るアドレスを入れる。

設定と接続情報は Quest の `/sdcard/ModData/com.beatgames.beatsaber/Mods/octantpath/`（`settings.json`、`connection.json`）に置く。選曲画面の板は上の縁の取っ手でつかんで動かせ、放した位置を `panel.json` に覚える。決まった位置に戻すには、`panel.json` の名前を変えて（例: `panel.json.bak`）からゲームを再起動する。

## お試し中の曲

OctantPath が入れた曲は「お試し中」として `ledger.json`（所有の記録、PC 版と同じ形）に書く。ゲームを起動してメインメニューが最初に開き、SongCore の最初の読み込みが終わったら、起動日を `state.json` に足して、お試し中の曲を決まりに沿って残すか消す（OctantPath 法 3）。消すのはメインメニューの画面が出ているあいだだけで、選曲やプレイに移ったら残りは次の起動で決める。消したら SongCore に読み直させる（プレイ中ならメニューに戻ってから）。PC 版は「起動直後、SongCore が曲を読む前の 1 回だけ消し、メニューにいる間は消さない」と決めてある（OctantSpecs の decisions.md、2026-10-08 の案 A）。Quest 版がメニューにいるあいだに消すのはこの決定の例外として記録してある（decisions.md、2026-10-23）。メニューで消してよいかは実機でまだ確かめていないので、試し運転を切る前に確かめる

- 残す: ゲームのお気に入りに入れたもの、遊んだ回数（ゲームの記録の、そのマップの全難易度の合計）が `keepMinPlays`（既定 3）以上のもの。記録を kept にするだけで、曲はそのまま
- 消す: 取ってから `trialExpiryLaunchDays`（既定 15）起動日を過ぎたもの。お試し中の合計が `trialMaxMegabytes`（既定 10240 MB）を超えるあいだは、古く取ったものから
- 消さない: ほかのプレイリストに入っているもの、中身が記録と違うもの。記録・設定・ゲームの記録・プレイリストのどれかが読めないとき、プレイリストの置き場所（`/sdcard/ModData/com.beatgames.beatsaber/Mods/PlaylistManager/Playlists` か `…/Mods/PlaylistCore/Playlists`）がひとつも見つからないときは、何も消さない
- プレイリストの置き場所は推測していて、実機では確かめていない。試し運転の `[janitor] playlists:` の行に、置き場所ごとの有無と守った曲の数が出るので、`deleteDryRun` を切る前に、自分のプレイリストが数えられているか確かめる
- `deleteDryRun`（既定 true）のあいだは、消す・残すの代わりに `adb logcat -s octantpath` の `[janitor]` の行に書くだけ。試し運転を止めるには `settings.json` に `"deleteDryRun": false` を書く

## ログ

```bash
adb logcat -s octantpath
```
