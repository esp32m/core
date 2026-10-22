"""Exercise the OTA command against a local HTTP server; never contact a MCU."""

import base64
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "ota-upload.py"
SPEC = importlib.util.spec_from_file_location("ota_upload", SCRIPT)
ota_upload = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ota_upload)


@contextmanager
def endpoint(status=200, body=b"OK", behavior=None):
    requests = []
    release = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            requests.append({
                "path": self.path,
                "headers": {key.lower(): value for key, value in self.headers.items()},
                "body": (b"" if behavior == "early_rejection" else
                         self.rfile.read(int(self.headers.get("Content-Length", 0)))),
            })
            if behavior == "drop":
                self.connection.shutdown(socket.SHUT_RDWR)
                self.connection.close()
                return
            if behavior == "timeout":
                release.wait(2)
            try:
                self.send_response(status)
                length = len(body) + (8 if behavior == "truncated_response" else 0)
                self.send_header("Content-Length", str(length))
                if status in (301, 302, 303, 307, 308):
                    self.send_header("Location", "/ota-upload")
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                pass  # Expected when the client times out or closes the socket.

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01})
    thread.start()
    try:
        yield "127.0.0.1:{}".format(server.server_port), requests
    finally:
        release.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class OtaUploadTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=Path(__file__).parent)
        self.addCleanup(self.directory.cleanup)
        self.firmware = Path(self.directory.name) / "application.bin"
        # Binary data larger than the client's transfer buffer catches multipart,
        # text conversion, truncation, and accidental chunked encoding.
        self.image = bytes(range(256)) * 129 + b"\x00\xff\r\n"
        self.firmware.write_bytes(self.image)

    def run_cli(self, host, firmware=None, options=()):
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = ota_upload.main([host, str(firmware or self.firmware), *options])
        return result, stdout.getvalue(), stderr.getvalue()

    def test_command_uploads_exact_raw_binary_and_reports_acceptance(self):
        with endpoint() as (host, requests):
            result = subprocess.run(
                [sys.executable, str(SCRIPT), host, str(self.firmware)],
                capture_output=True, text=True, timeout=5,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("OTA update accepted", result.stdout)
        self.assertIn("reboot automatically", result.stdout)
        self.assertEqual(result.stderr, "")
        self.assertEqual(len(requests), 1)
        request = requests[0]
        self.assertEqual(request["path"], "/ota-upload")
        self.assertEqual(request["body"], self.image)
        self.assertEqual(request["headers"]["content-length"], str(len(self.image)))
        self.assertEqual(request["headers"]["content-type"], "application/octet-stream")
        self.assertNotIn("transfer-encoding", request["headers"])

    def test_basic_auth_is_sent_with_the_first_and_only_request(self):
        with endpoint() as (host, requests):
            result, _, error = self.run_cli(
                host, options=("--username", "admin", "--password", "p\u00e4ss:word")
            )
        self.assertEqual(result, 0, error)
        self.assertEqual(len(requests), 1)
        expected = "Basic " + base64.b64encode("admin:p\u00e4ss:word".encode()).decode()
        self.assertEqual(requests[0]["headers"]["authorization"], expected)

    def test_server_rejections_fail_without_retry(self):
        for status, body in ((403, b"Vendor only"), (500, b"Busy"),
                             (500, b"ESP_ERR_OTA_VALIDATE_FAILED")):
            with self.subTest(status=status, body=body):
                with endpoint(status, body) as (host, requests):
                    result, output, error = self.run_cli(host)
                self.assertEqual(result, 1)
                self.assertIn("HTTP {}".format(status), error)
                self.assertIn(body.decode(), error)
                self.assertNotIn("OTA update accepted", output)
                self.assertEqual(len(requests), 1)

    def test_redirects_are_never_followed(self):
        for status in (301, 302, 303, 307, 308):
            with self.subTest(status=status):
                with endpoint(status, b"redirect") as (host, requests):
                    result, _, error = self.run_cli(host)
                self.assertEqual(result, 1)
                self.assertIn("HTTP {}".format(status), error)
                self.assertEqual(len(requests), 1)

    def test_rejection_before_reading_the_image_cannot_report_success_or_retry(self):
        self.firmware.write_bytes(self.image * 128)
        with endpoint(500, b"Busy", behavior="early_rejection") as (host, requests):
            result, output, error = self.run_cli(host)
        self.assertEqual(result, 1)
        self.assertIn("update not confirmed", error)
        self.assertNotIn("OTA update accepted", output)
        self.assertEqual(len(requests), 1)
        self.assertEqual(requests[0]["body"], b"")

    def test_http_200_requires_the_ota_acknowledgement(self):
        for body in (b"", b"<html>login</html>", b"OK but failed", b"OK\n"):
            with self.subTest(body=body):
                with endpoint(body=body) as (host, requests):
                    result, output, error = self.run_cli(host)
                self.assertEqual(result, 1)
                self.assertIn("update not confirmed", error)
                self.assertNotIn("OTA update accepted", output)
                self.assertEqual(len(requests), 1)

    def test_truncated_response_cannot_confirm_the_update(self):
        with endpoint(behavior="truncated_response") as (host, requests):
            result, output, error = self.run_cli(host)
        self.assertEqual(result, 1)
        self.assertIn("update not confirmed", error)
        self.assertNotIn("OTA update accepted", output)
        self.assertEqual(len(requests), 1)

    def test_dropped_response_is_uncertain_and_never_retried(self):
        with endpoint(behavior="drop") as (host, requests):
            result, output, error = self.run_cli(host)
        self.assertEqual(result, 1)
        self.assertIn("update not confirmed", error)
        self.assertIn("check its state before retrying", error)
        self.assertNotIn("OTA update accepted", output)
        self.assertEqual(len(requests), 1)
        self.assertEqual(requests[0]["body"], self.image)

    def test_response_timeout_is_uncertain_and_never_retried(self):
        with endpoint(behavior="timeout") as (host, requests):
            result, _, error = self.run_cli(host, options=("--timeout", "0.05"))
        self.assertEqual(result, 1)
        self.assertIn("timed out", error)
        self.assertIn("check its state before retrying", error)
        self.assertEqual(len(requests), 1)

    def test_missing_or_empty_file_never_reaches_the_server(self):
        empty = Path(self.directory.name) / "empty.bin"
        empty.touch()
        missing = Path(self.directory.name) / "missing.bin"
        with endpoint() as (host, requests):
            for firmware in (missing, empty):
                with self.subTest(firmware=firmware.name):
                    result, output, error = self.run_cli(host, firmware)
                    self.assertEqual(result, 1)
                    self.assertTrue(error)
                    self.assertNotIn("Uploading", output)
            self.assertEqual(requests, [])

    def test_invalid_hosts_and_timeouts_are_cli_usage_errors(self):
        invalid_hosts = ("", "ftp://localhost", "localhost:0", "localhost:65536",
                         "http://localhost/path", "http://user:pass@localhost", "host name")
        with endpoint() as (host, requests):
            cases = [(value, ()) for value in invalid_hosts]
            cases += [(host, ("--timeout", value))
                      for value in ("0", "-1", "nan", "inf", "invalid")]
            for value, options in cases:
                with self.subTest(host=value, options=options):
                    with self.assertRaises(SystemExit) as caught:
                        self.run_cli(value, options=options)
                    self.assertEqual(caught.exception.code, 2)
            self.assertEqual(requests, [])


if __name__ == "__main__":
    unittest.main()
