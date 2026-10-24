#!/usr/bin/env python3
"""Quest 版 OctantPath の PC のプロキシ。

ネットにつながない Quest の MOD が組み立てた BeatSaver への要求を、PC が https で中継して返す。
Quest からは USB の adb reverse で 127.0.0.1 に届く。中身は解釈しない（照会・取得の判断と hash の照合は MOD がする）。
守ること（OctantSpecs contracts/external.md の BeatSaver API の行と、OctantPath 法 2.1・2.8）:
- 行き先は https の beatsaver.com とその下だけ。転送の先も確かめる
- API（api.beatsaver.com）への要求は直列にして間隔を空ける。zip などの CDN は同時数と 1 秒あたりの開始数に上限を設ける
- 1 つの応答の大きさと待ち時間に上限を設け、どの部品か分かる User-Agent を付ける
- 取得元をローカルのフォルダにしたときは、そこを読むだけで、書き込まず、消さない
Python 3 の標準ライブラリだけで動く。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import BinaryIO, Callable, Optional

VERSION = "0.1.0"
USER_AGENT = f"OctantPath/{VERSION} (Quest proxy)"
DEFAULT_PORT = 23288
API_HOST = "api.beatsaver.com"
HASH_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
CHUNK = 64 * 1024
MIN_API_INTERVAL_SECONDS = 1.0


@dataclass
class Settings:
    """数値はすべて設定 JSON で変えられる。名前は PC 版 OctantPath の設定と同じ"""

    port: int = DEFAULT_PORT
    beatSaverApiIntervalSeconds: float = 1.0
    downloadConcurrency: int = 2
    downloadStartsPerSecond: float = 2.0
    zipMaxDownloadMegabytes: int = 100
    cdnTimeoutSeconds: float = 30.0
    # "BeatSaver" か、`<hash の先頭 2 文字>/<hash>.zip` の形のフォルダ
    zipSource: str = "BeatSaver"

    @staticmethod
    def load(path: Optional[Path]) -> "Settings":
        settings = Settings()
        if path is None or not path.exists():
            return settings
        values = json.loads(path.read_text(encoding="utf-8"))
        for name, current in vars(settings).items():
            if name in values and values[name] is not None:
                setattr(settings, name, type(current)(values[name]))
        return settings

    @property
    def max_bytes(self) -> int:
        return self.zipMaxDownloadMegabytes * 1024 * 1024

    @property
    def local_zip_folder(self) -> Optional[Path]:
        return None if self.zipSource == "BeatSaver" else Path(self.zipSource)


def allowed(url: str) -> bool:
    """https で、ホストが beatsaver.com かその下だけ（PC 版 OctantPath の BeatSaverUrls と同じ）"""
    try:
        parts = urllib.parse.urlsplit(url)
    except ValueError:
        return False
    if parts.scheme != "https" or not parts.hostname or parts.username or parts.password:
        return False
    host = parts.hostname.rstrip(".").lower()
    return host == "beatsaver.com" or host.endswith(".beatsaver.com")


def is_api(url: str) -> bool:
    return (urllib.parse.urlsplit(url).hostname or "").rstrip(".").lower() == API_HOST


def local_zip_path(folder: Path, hash_: str) -> Optional[Path]:
    """フォルダの `<hash の先頭 2 文字>/<hash>.zip`。hash の大文字と小文字は区別しない"""
    if not HASH_PATTERN.match(hash_):
        return None
    for name in (hash_.lower(), hash_.upper()):
        for prefix in (name[:2].lower(), name[:2].upper()):
            path = folder / prefix / f"{name}.zip"
            if path.is_file():
                return path
    return None


class Throttle:
    """同時数と開始の間隔の上限。API には同時数 1 と間隔、CDN には設定の同時数と 1 秒あたりの開始数"""

    def __init__(self, concurrency: int, spacing: float, clock: Callable[[], float] = time.monotonic, sleep: Callable[[float], None] = time.sleep):
        self._slots = threading.BoundedSemaphore(max(1, concurrency))
        self._spacing = max(0.0, spacing)
        self._clock = clock
        self._sleep = sleep
        self._lock = threading.Lock()
        self._next_start = 0.0

    def __enter__(self):
        self._slots.acquire()
        with self._lock:
            wait = self._next_start - self._clock()
            if wait > 0:
                self._sleep(wait)
            self._next_start = self._clock() + self._spacing
        return self

    def __exit__(self, *exc):
        self._slots.release()
        return False


class _CheckedRedirect(urllib.request.HTTPRedirectHandler):
    """転送の先も https の beatsaver.com とその下だけ"""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not allowed(newurl):
            raise urllib.error.HTTPError(newurl, 403, "redirect to a host that is not allowed", headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class TooLarge(Exception):
    pass


@dataclass
class Upstream:
    status: int
    content_type: Optional[str]
    length: Optional[int]
    body: BinaryIO


class Forwarder:
    """BeatSaver に GET を送る。開くのは差し替えられる（テスト）"""

    def __init__(self, settings: Settings, opener: Optional[Callable[..., object]] = None):
        self.settings = settings
        self._open = opener or urllib.request.build_opener(_CheckedRedirect()).open
        # API の間隔は 1 秒より短くしない（PC 版と同じ）
        self.api = Throttle(1, max(MIN_API_INTERVAL_SECONDS, settings.beatSaverApiIntervalSeconds))
        self.cdn = Throttle(settings.downloadConcurrency, 1.0 / max(0.1, settings.downloadStartsPerSecond))

    def fetch(self, url: str, write: Callable[[Upstream], Callable[[bytes], None]]) -> None:
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT}, method="GET")
        with self.api if is_api(url) else self.cdn:
            try:
                response = self._open(request, timeout=self.settings.cdnTimeoutSeconds)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                status = getattr(response, "status", None) or response.getcode()
                length = response.headers.get("Content-Length")
                length = int(length) if length and length.isdigit() else None
                if length is not None and length > self.settings.max_bytes:
                    raise TooLarge(f"{length} bytes")
                sink = write(Upstream(status, response.headers.get("Content-Type"), length, response))
                total = 0
                while chunk := response.read(CHUNK):
                    total += len(chunk)
                    if total > self.settings.max_bytes:
                        raise TooLarge(f"more than {self.settings.max_bytes} bytes")
                    sink(chunk)


def make_handler(forwarder: Forwarder, log: Callable[[str], None]):
    settings = forwarder.settings

    class Handler(BaseHTTPRequestHandler):
        server_version = f"OctantPathAndroidProxy/{VERSION}"

        def log_message(self, format, *args):  # noqa: A002 - BaseHTTPRequestHandler の名前
            log(format % args)

        def _json(self, status: int, body: dict) -> None:
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _error(self, status: int, code: str, message: str) -> None:
            self._json(status, {"error": {"code": code, "message": message}})

        def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler の名前
            parts = urllib.parse.urlsplit(self.path)
            if parts.path == "/v1/health":
                return self._json(200, {"proxy": "octantpath-android", "version": VERSION, "localZip": settings.local_zip_folder is not None})
            if parts.path == "/v1/beatsaver":
                return self._beatsaver(urllib.parse.parse_qs(parts.query).get("url", [""])[0])
            if parts.path.startswith("/v1/local-zip/"):
                return self._local_zip(parts.path[len("/v1/local-zip/"):])
            self._error(404, "not_found", "unknown path")

        def _beatsaver(self, url: str) -> None:
            if not allowed(url):
                return self._error(403, "not_allowed", "only https on beatsaver.com and its subdomains")
            started = []

            def write(upstream: Upstream):
                self.send_response(upstream.status)
                if upstream.content_type:
                    self.send_header("Content-Type", upstream.content_type)
                if upstream.length is not None:
                    self.send_header("Content-Length", str(upstream.length))
                self.send_header("Connection", "close")
                self.end_headers()
                started.append(True)
                return self.wfile.write

            try:
                forwarder.fetch(url, write)
            except TooLarge as error:
                if not started:
                    return self._error(413, "too_large", str(error))
                log(f"{url}: stopped, {error}")
            except (urllib.error.URLError, OSError, TimeoutError) as error:
                if not started:
                    return self._error(502, "unreachable", str(getattr(error, "reason", error)))
                log(f"{url}: stopped, {error}")
            self.close_connection = True

        def _local_zip(self, hash_: str) -> None:
            folder = settings.local_zip_folder
            if folder is None:
                return self._error(404, "no_local_zip", "zipSource is BeatSaver")
            path = local_zip_path(folder, hash_)
            if path is None:
                return self._error(404, "not_found", "the zip is not in the folder")
            size = path.stat().st_size
            if size > settings.max_bytes:
                return self._error(413, "too_large", f"{size} bytes")
            self.send_response(200)
            self.send_header("Content-Type", "application/zip")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with path.open("rb") as file:
                while chunk := file.read(CHUNK):
                    self.wfile.write(chunk)

    return Handler


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description="Quest 版 OctantPath の PC のプロキシ（BeatSaver を https で中継する）")
    parser.add_argument("--settings", type=Path, help="設定 JSON（省くと既定値）")
    parser.add_argument("--port", type=int, help=f"待ち受けるポート（既定 {DEFAULT_PORT}）")
    args = parser.parse_args(argv)
    settings = Settings.load(args.settings)
    if args.port:
        settings.port = args.port
    folder = settings.local_zip_folder
    if folder is not None and not folder.is_dir():
        print(f"zipSource のフォルダがありません: {folder}", file=sys.stderr)
        return 1

    def log(message: str) -> None:
        print(f"{time.strftime('%H:%M:%S')} {message}", file=sys.stderr, flush=True)

    server = ThreadingHTTPServer(("127.0.0.1", settings.port), make_handler(Forwarder(settings), log))
    log(f"OctantPath Android proxy {VERSION} on 127.0.0.1:{settings.port} (zipSource: {settings.zipSource})")
    log(f"Quest からは: adb reverse tcp:{settings.port} tcp:{settings.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
