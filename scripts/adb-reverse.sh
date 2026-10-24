#!/usr/bin/env bash
# USB でつないだ Quest の 127.0.0.1 を PC に届ける。OctantCompass（23287）と PC のプロキシ（23288）。
# adb reverse は Quest の再起動や USB の抜き差しで消えるので、つなぎ直すたびに実行する
set -euo pipefail
adb reverse tcp:23287 tcp:23287
adb reverse tcp:23288 tcp:23288
adb reverse --list
