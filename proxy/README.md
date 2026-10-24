# PC のプロキシ

Quest をネットにつながないまま、Quest 版 OctantPath が BeatSaver（曲名・カバー・試聴・zip）を引けるようにする。Quest の MOD が組み立てた要求を、PC が https で BeatSaver に中継して返す。中身は解釈せず、照会や取得の判断と hash の照合は MOD がする。

守ること（OctantSpecs の contracts/external.md の BeatSaver API の行と、OctantPath 法 2.1・2.8）:

- 行き先は https の beatsaver.com とその下だけ。転送の先も確かめる
- API（api.beatsaver.com）は直列で、`beatSaverApiIntervalSeconds` の間隔を空ける（1 秒より短くはしない。PC 版と同じ）。CDN（zip・カバー・試聴）は `downloadConcurrency` と `downloadStartsPerSecond` の上限で取る
- 1 つの応答は `zipMaxDownloadMegabytes` まで、待ち時間は `cdnTimeoutSeconds` まで。User-Agent は `OctantPath/<版> (Quest proxy)`
- `zipSource` をフォルダ（`<hash の先頭 2 文字>/<hash>.zip`）にすると、zip をそこから渡す。フォルダは読むだけ

## 使い方

```bash
python3 proxy/octantpath_proxy.py                       # 127.0.0.1:23288 で待ち受ける
adb reverse tcp:23288 tcp:23288                       # Quest の 127.0.0.1:23288 を PC に届ける
python3 proxy/octantpath_proxy.py --settings proxy.json # 数値や zipSource を変えるとき
```

設定 JSON の項目の名前は PC 版 OctantPath の設定と同じ（`port` だけ足した）。省いた項目は既定値。

MOD は `beatSaverProxyHost`・`beatSaverProxyPort`（MOD の settings.json、既定は 127.0.0.1:23288）のプロキシに頼む。届かなければ、推薦は導入済みの曲だけを出す。

## MOD から見た口

| 要求 | 応答 |
|---|---|
| `GET /v1/health` | `{"proxy": "octantpath-android", "version", "localZip"}` |
| `GET /v1/beatsaver?url=<https の URL>` | BeatSaver の応答の状態・Content-Type・本文をそのまま。許さない行き先は 403（`not_allowed`）、届かなければ 502（`unreachable`）、大きすぎれば 413（`too_large`） |
| `GET /v1/local-zip/<hash>` | `zipSource` のフォルダの zip。フォルダを使っていなければ 404（`no_local_zip`） |

エラーの本文は `{"error": {"code", "message"}}`。

## テスト

```bash
python3 -I -m unittest discover -s proxy/tests -v
```
