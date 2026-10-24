import io
import json
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import octantpath_proxy as proxy  # noqa: E402

HASH = "a" * 40


class FakeResponse(io.BytesIO):
    def __init__(self, body: bytes, status: int = 200, content_type: str = "application/json", length: bool = True):
        super().__init__(body)
        self.status = status
        self.headers = {"Content-Type": content_type}
        if length:
            self.headers["Content-Length"] = str(len(body))

    def getcode(self):
        return self.status


class FakeOpener:
    def __init__(self, responses):
        self.responses = responses
        self.requests = []

    def __call__(self, request, timeout):
        self.requests.append(request)
        response = self.responses[request.full_url]
        if isinstance(response, Exception):
            raise response
        return response


class Server:
    """本物の HTTP で待ち受けるプロキシ（BeatSaver への送り先だけ差し替える）"""

    def __init__(self, settings, opener):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), proxy.make_handler(proxy.Forwarder(settings, opener), lambda _: None))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def get(self, path):
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{self.server.server_address[1]}{path}", timeout=5) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class AllowedTests(unittest.TestCase):
    def test_only_https_beatsaver(self):
        self.assertTrue(proxy.allowed("https://api.beatsaver.com/maps/hash/" + HASH))
        self.assertTrue(proxy.allowed("https://r2cdn.beatsaver.com/" + HASH + ".zip"))
        self.assertTrue(proxy.allowed("https://beatsaver.com/x"))
        self.assertTrue(proxy.allowed("https://CDN.BeatSaver.com./x"))
        self.assertFalse(proxy.allowed("http://api.beatsaver.com/maps"))
        self.assertFalse(proxy.allowed("https://evilbeatsaver.com/x"))
        self.assertFalse(proxy.allowed("https://beatsaver.com.evil.example/x"))
        self.assertFalse(proxy.allowed("https://user@beatsaver.com/x"))
        self.assertFalse(proxy.allowed("file:///etc/passwd"))
        self.assertFalse(proxy.allowed(""))


class ThrottleTests(unittest.TestCase):
    def test_spacing_between_starts(self):
        now = [0.0]
        slept = []
        throttle = proxy.Throttle(1, 1.0, clock=lambda: now[0], sleep=lambda s: (slept.append(s), now.__setitem__(0, now[0] + s)))
        with throttle:
            pass
        now[0] += 0.25
        with throttle:
            pass
        self.assertEqual(slept, [0.75])

    def test_api_interval_is_at_least_one_second(self):
        forwarder = proxy.Forwarder(proxy.Settings(beatSaverApiIntervalSeconds=0), opener=lambda *a, **k: None)
        self.assertEqual(forwarder.api._spacing, 1.0)
        forwarder = proxy.Forwarder(proxy.Settings(beatSaverApiIntervalSeconds=2.5), opener=lambda *a, **k: None)
        self.assertEqual(forwarder.api._spacing, 2.5)


class LocalZipTests(unittest.TestCase):
    def test_finds_zip_in_hash_folder_and_rejects_bad_names(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "aa").mkdir()
            (Path(folder) / "aa" / f"{HASH}.zip").write_bytes(b"zip")
            self.assertEqual(proxy.local_zip_path(Path(folder), HASH.upper()).read_bytes(), b"zip")
            self.assertIsNone(proxy.local_zip_path(Path(folder), "b" * 40))
            self.assertIsNone(proxy.local_zip_path(Path(folder), "../../etc/passwd"))


class ForwardTests(unittest.TestCase):
    def setUp(self):
        self.settings = proxy.Settings(downloadStartsPerSecond=1000, zipMaxDownloadMegabytes=1)

    def serve(self, responses):
        self.opener = FakeOpener(responses)
        server = Server(self.settings, self.opener)
        self.addCleanup(server.close)
        return server

    def test_forwards_body_status_and_user_agent(self):
        url = "https://api.beatsaver.com/maps/hash/" + HASH
        server = self.serve({url: FakeResponse(b'{"id":"1"}')})
        status, body = server.get("/v1/beatsaver?url=" + urllib.request.quote(url, safe=""))
        self.assertEqual((status, body), (200, b'{"id":"1"}'))
        self.assertEqual(self.opener.requests[0].get_header("User-agent"), proxy.USER_AGENT)

    def test_passes_upstream_errors_through(self):
        url = "https://api.beatsaver.com/maps/hash/" + HASH
        server = self.serve({url: FakeResponse(b'{"error":"Not Found"}', status=404)})
        status, body = server.get("/v1/beatsaver?url=" + urllib.request.quote(url, safe=""))
        self.assertEqual(status, 404)
        self.assertEqual(json.loads(body), {"error": "Not Found"})

    def test_refuses_other_hosts_without_sending(self):
        server = self.serve({})
        status, body = server.get("/v1/beatsaver?url=" + urllib.request.quote("https://example.com/x", safe=""))
        self.assertEqual(status, 403)
        self.assertEqual(json.loads(body)["error"]["code"], "not_allowed")
        self.assertEqual(self.opener.requests, [])

    def test_refuses_too_large_by_content_length(self):
        url = "https://r2cdn.beatsaver.com/" + HASH + ".zip"
        big = FakeResponse(b"x" * 10, content_type="application/zip")
        big.headers["Content-Length"] = str(2 * 1024 * 1024)
        server = self.serve({url: big})
        status, body = server.get("/v1/beatsaver?url=" + urllib.request.quote(url, safe=""))
        self.assertEqual(status, 413)

    def test_unreachable_is_502(self):
        url = "https://api.beatsaver.com/maps/hash/" + HASH
        server = self.serve({url: urllib.error.URLError("no network")})
        status, body = server.get("/v1/beatsaver?url=" + urllib.request.quote(url, safe=""))
        self.assertEqual(status, 502)
        self.assertEqual(json.loads(body)["error"]["code"], "unreachable")

    def test_local_zip(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "aa").mkdir()
            (Path(folder) / "aa" / f"{HASH}.zip").write_bytes(b"PK zip")
            self.settings.zipSource = folder
            server = self.serve({})
            self.assertEqual(server.get(f"/v1/local-zip/{HASH}"), (200, b"PK zip"))
            self.assertEqual(server.get("/v1/local-zip/" + "b" * 40)[0], 404)
            status, body = server.get("/v1/health")
            self.assertTrue(json.loads(body)["localZip"])

    def test_no_local_zip_when_source_is_beatsaver(self):
        server = self.serve({})
        status, body = server.get(f"/v1/local-zip/{HASH}")
        self.assertEqual(status, 404)
        self.assertEqual(json.loads(body)["error"]["code"], "no_local_zip")


class RedirectTests(unittest.TestCase):
    def test_redirect_to_other_host_is_refused(self):
        handler = proxy._CheckedRedirect()
        request = urllib.request.Request("https://r2cdn.beatsaver.com/x.zip")
        with self.assertRaises(urllib.error.HTTPError):
            handler.redirect_request(request, io.BytesIO(), 302, "Found", {}, "https://example.com/x.zip")
        self.assertIsNotNone(handler.redirect_request(request, io.BytesIO(), 302, "Found", {}, "https://cdn.beatsaver.com/x.zip"))


class SettingsTests(unittest.TestCase):
    def test_reads_known_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.json"
            path.write_text(json.dumps({"port": 24000, "downloadConcurrency": 4, "unknown": 1}))
            settings = proxy.Settings.load(path)
            self.assertEqual((settings.port, settings.downloadConcurrency, settings.zipSource), (24000, 4, "BeatSaver"))


if __name__ == "__main__":
    unittest.main()
