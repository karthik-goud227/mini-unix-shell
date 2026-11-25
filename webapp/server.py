#!/usr/bin/env python3
"""Local browser interface for msh. Uses only the Python standard library."""

import argparse
import base64
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import shlex
import shutil
import signal
import struct
import subprocess
import tempfile
import termios
import threading
import time
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit


ROOT = Path(__file__).resolve().parent.parent
STATIC = Path(__file__).resolve().parent / "static"
MAX_OUTPUT = 1_000_000
MAX_REQUEST = 65_536
CHILD_BOOTSTRAP = (
    "import fcntl, os, sys, termios; "
    "os.setsid(); "
    "fcntl.ioctl(0, termios.TIOCSCTTY, 0); "
    "os.chdir(sys.argv[1]); "
    "os.execv(sys.argv[2], [sys.argv[2]])"
)


class ShellSession:
    def __init__(self):
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.buffer = bytearray()
        self.start_offset = 0
        self.total_offset = 0
        self.master = None
        self.pid = None
        self.alive = False
        self.exit_status = None
        self.generation = 0
        self.script_dir = Path(tempfile.mkdtemp(prefix="miniunix-lab-"))
        self.script_number = 0
        self.spawn()

    def _append(self, data):
        with self.lock:
            self.buffer.extend(data)
            self.total_offset += len(data)
            if len(self.buffer) > MAX_OUTPUT:
                excess = len(self.buffer) - MAX_OUTPUT
                del self.buffer[:excess]
                self.start_offset += excess

    def spawn(self):
        with self.lock:
            if self.alive:
                raise RuntimeError("The shell is already running")
        master, slave = pty.openpty()
        environment = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
        actions = [(os.POSIX_SPAWN_DUP2, slave, descriptor)
                   for descriptor in (0, 1, 2)]
        actions.extend(((os.POSIX_SPAWN_CLOSE, master),
                        (os.POSIX_SPAWN_CLOSE, slave)))
        try:
            pid = os.posix_spawn(
                sys.executable,
                [sys.executable, "-c", CHILD_BOOTSTRAP, str(ROOT), str(ROOT / "msh")],
                environment, file_actions=actions)
        except Exception:
            os.close(master)
            raise
        finally:
            os.close(slave)
        with self.lock:
            self.pid = pid
            self.master = master
            self.alive = True
            self.exit_status = None
            self.generation += 1
            generation = self.generation
        reader = threading.Thread(target=self._read_output,
                                  args=(pid, master, generation), daemon=True)
        reader.start()

    def _read_output(self, pid, master, generation):
        try:
            while True:
                try:
                    data = os.read(master, 16_384)
                except OSError as exc:
                    if exc.errno in (errno.EIO, errno.EBADF):
                        break
                    raise
                if not data:
                    break
                self._append(data)
        finally:
            try:
                _, status = os.waitpid(pid, 0)
                code = os.waitstatus_to_exitcode(status)
            except ChildProcessError:
                code = None
            try:
                os.close(master)
            except OSError:
                pass
            with self.lock:
                if self.generation == generation:
                    self.alive = False
                    self.master = None
                    self.exit_status = code

    def output_since(self, cursor):
        with self.lock:
            truncated = cursor < self.start_offset
            cursor = max(self.start_offset, min(cursor, self.total_offset))
            start = cursor - self.start_offset
            chunk = bytes(self.buffer[start:start + 65_536])
            return {
                "data": base64.b64encode(chunk).decode("ascii"),
                "cursor": cursor + len(chunk),
                "truncated": truncated,
                "alive": self.alive,
                "exit_status": self.exit_status,
            }

    def details(self):
        with self.lock:
            pid, alive, code = self.pid, self.alive, self.exit_status
        try:
            directory = os.readlink(f"/proc/{pid}/cwd") if alive else None
        except OSError:
            directory = None
        return {"alive": alive, "pid": pid, "cwd": directory,
                "exit_status": code}

    def send(self, data):
        if not isinstance(data, str) or len(data.encode("utf-8")) > 4096:
            raise ValueError("Input must be text of at most 4096 bytes")
        with self.lock:
            master = self.master if self.alive else None
        if master is None:
            raise RuntimeError("The shell has exited. Start a new session.")
        encoded = data.encode("utf-8")
        with self.write_lock:
            while encoded:
                encoded = encoded[os.write(master, encoded):]

    def resize(self, cols, rows):
        if not (20 <= cols <= 300 and 8 <= rows <= 100):
            raise ValueError("Terminal size is outside the supported range")
        with self.lock:
            master = self.master if self.alive else None
        if master is not None:
            fcntl.ioctl(master, termios.TIOCSWINSZ,
                        struct.pack("HHHH", rows, cols, 0, 0))

    def run_script(self, source):
        if not isinstance(source, str) or not source.strip():
            raise ValueError("Write a script before running it")
        if len(source.encode("utf-8")) > MAX_REQUEST:
            raise ValueError("Script is too large")
        with self.lock:
            self.script_number += 1
            number = self.script_number
        path = self.script_dir / f"snippet-{number}.msh"
        path.write_text(source + ("" if source.endswith("\n") else "\n"),
                        encoding="utf-8")
        self.send(f"./msh {shlex.quote(str(path))}\n")
        return path.name

    def shutdown(self):
        with self.lock:
            pid, master, alive = self.pid, self.master, self.alive
        if alive and master is not None:
            try:
                os.write(master, b"exit\nexit\n")
            except OSError:
                pass
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                with self.lock:
                    if not self.alive:
                        break
                time.sleep(0.05)
            with self.lock:
                alive = self.alive
            if alive:
                try:
                    os.killpg(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        shutil.rmtree(self.script_dir, ignore_errors=True)


class AppServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address):
        super().__init__(address, Handler)
        self.session = ShellSession()


class Handler(BaseHTTPRequestHandler):
    server_version = "MiniUnixLab"

    def log_message(self, format_string, *args):
        if not self.path.startswith("/api/output"):
            super().log_message(format_string, *args)

    def _host_allowed(self):
        host = self.headers.get("Host", "").split(":", 1)[0].lower()
        return host in ("127.0.0.1", "localhost")

    def _origin_allowed(self):
        origin = self.headers.get("Origin")
        if origin is None:
            return True
        return origin in (f"http://127.0.0.1:{self.server.server_port}",
                          f"http://localhost:{self.server.server_port}")

    def _headers(self, status, content_type, length):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy",
                         "default-src 'self'; connect-src 'self'; "
                         "script-src 'self'; style-src 'self' 'unsafe-inline'; "
                         "img-src 'self' data:; font-src 'self' data:")
        self.end_headers()

    def _reply(self, status, value):
        body = json.dumps(value).encode("utf-8")
        self._headers(status, "application/json; charset=utf-8", len(body))
        self.wfile.write(body)

    def _check_request(self, post=False):
        if not self._host_allowed() or (post and not self._origin_allowed()):
            self._reply(403, {"error": "Local access only"})
            return False
        return True

    def do_GET(self):
        if not self._check_request():
            return
        url = urlsplit(self.path)
        if url.path == "/api/session":
            self._reply(200, self.server.session.details())
            return
        if url.path == "/api/output":
            try:
                cursor = int(parse_qs(url.query).get("cursor", ["0"])[0])
                if cursor < 0:
                    raise ValueError
            except ValueError:
                self._reply(400, {"error": "Invalid output cursor"})
                return
            self._reply(200, self.server.session.output_since(cursor))
            return
        if url.path == "/":
            url = url._replace(path="/index.html")
        if not url.path.startswith("/static/") and url.path != "/index.html":
            self._reply(404, {"error": "Not found"})
            return
        relative = url.path.removeprefix("/")
        file_path = (STATIC / relative.removeprefix("static/")).resolve()
        if not file_path.is_relative_to(STATIC.resolve()) or not file_path.is_file():
            self._reply(404, {"error": "Not found"})
            return
        suffix = file_path.suffix
        content_type = {".html": "text/html", ".css": "text/css",
                        ".js": "text/javascript", ".svg": "image/svg+xml"}.get(
                            suffix, "application/octet-stream") + "; charset=utf-8"
        body = file_path.read_bytes()
        self._headers(200, content_type, len(body))
        self.wfile.write(body)

    def do_POST(self):
        if not self._check_request(post=True):
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            # JSON escaping can make a 64 KB script substantially larger.
            if length <= 0 or length > MAX_REQUEST * 6 + 1024:
                raise ValueError("Request is too large or empty")
            request = json.loads(self.rfile.read(length))
            if not isinstance(request, dict):
                raise ValueError("Expected a JSON object")
            path = urlsplit(self.path).path
            if path == "/api/input":
                self.server.session.send(request.get("data"))
                result = {"ok": True}
            elif path == "/api/resize":
                self.server.session.resize(int(request["cols"]),
                                           int(request["rows"]))
                result = {"ok": True}
            elif path == "/api/script":
                result = {"ok": True,
                          "filename": self.server.session.run_script(
                              request.get("source"))}
            elif path == "/api/restart":
                self.server.session.spawn()
                result = {"ok": True}
            else:
                self._reply(404, {"error": "Not found"})
                return
            self._reply(200, result)
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            self._reply(400, {"error": str(exc)})
        except (RuntimeError, OSError) as exc:
            self._reply(409, {"error": str(exc)})


def main():
    parser = argparse.ArgumentParser(description="MiniUnix Shell Lab")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-build", action="store_true",
                        help="Use an existing msh binary (for testing)")
    args = parser.parse_args()
    if not (0 <= args.port <= 65535):
        parser.error("port must be 0 through 65535")
    if not args.no_build:
        print("Building msh…", flush=True)
        subprocess.run(["make", "msh"], cwd=ROOT, check=True)
    server = AppServer(("127.0.0.1", args.port))
    print(f"MiniUnix Shell Lab ready at http://127.0.0.1:{server.server_port}",
          flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        print("\nStopping MiniUnix Shell Lab…", flush=True)
    finally:
        server.server_close()
        server.session.shutdown()


if __name__ == "__main__":
    main()
