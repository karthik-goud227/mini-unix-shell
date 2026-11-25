"""Integration checks for the local browser bridge."""

import base64
import json
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import AppServer


class WebAppTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = AppServer(("127.0.0.1", 0))
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.server.session.shutdown()
        cls.thread.join(timeout=3)

    def call(self, path, data=None, headers=None):
        body = None if data is None else json.dumps(data).encode()
        request = Request(self.url + path, body, headers or {})
        if body is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urlopen(request, timeout=3) as response:
                raw = response.read()
                return response.status, (json.loads(raw) if path.startswith("/api/") else raw)
        except HTTPError as error:
            return error.code, json.loads(error.read())

    def wait_for_output(self, expected, cursor):
        output = bytearray()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            status, result = self.call(f"/api/output?cursor={cursor}")
            self.assertEqual(status, 200)
            cursor = result["cursor"]
            output.extend(base64.b64decode(result["data"]))
            if expected in output:
                return cursor
            time.sleep(0.04)
        self.fail(f"Expected {expected!r} in {bytes(output)!r}")

    def test_browser_bridge(self):
        status, page = self.call("/")
        self.assertEqual(status, 200)
        self.assertIn(b"MiniUnix Shell Lab", page)
        self.assertEqual(self.call("/api/session")[1]["alive"], True)
        cursor = self.wait_for_output(b"msh> ", 0)

        self.assertEqual(self.call("/api/resize", {"cols": 80, "rows": 24})[0], 200)
        self.assertEqual(self.call("/api/input", {"data": "echo WEBAPP_OK\n"})[0], 200)
        cursor = self.wait_for_output(b"WEBAPP_OK\r\n", cursor)

        source = "for ITEM in one two\ndo\necho $ITEM\ndone\n"
        status, result = self.call("/api/script", {"source": source})
        self.assertEqual(status, 200)
        self.assertTrue(result["filename"].endswith(".msh"))
        cursor = self.wait_for_output(b"two\r\n", cursor)

        self.assertEqual(self.call("/api/output?cursor=-1")[0], 400)
        self.assertEqual(self.call("/api/input", {"data": "x"},
                                   {"Origin": "http://example.com"})[0], 403)
        self.assertEqual(self.call("/api/session", headers={"Host": "example.com"})[0], 403)

        self.assertEqual(self.call("/api/input", {"data": "exit\n"})[0], 200)
        deadline = time.monotonic() + 5
        while self.call("/api/session")[1]["alive"] and time.monotonic() < deadline:
            time.sleep(0.04)
        self.assertFalse(self.call("/api/session")[1]["alive"])
        self.assertEqual(self.call("/api/restart", {})[0], 200)
        self.assertTrue(self.call("/api/session")[1]["alive"])
        self.wait_for_output(b"msh> ", cursor)


if __name__ == "__main__":
    unittest.main()
