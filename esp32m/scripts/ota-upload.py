#!/usr/bin/env python3
"""Upload an application .bin to esp32m's HTTP OTA endpoint (stdlib only).

Usage:
    python ota-upload.py esp32m.local path/to/application.bin
    python ota-upload.py 192.168.1.50:8080 path/to/application.bin --timeout 180
    python ota-upload.py esp32m.local application.bin --username admin

The MCU validates the image, selects it for boot, replies OK, then reboots.
There are no automatic retries or redirects. Success confirms acceptance by
the OTA endpoint; it does not verify that the new firmware boots successfully.
"""

import argparse
import base64
import getpass
import http.client
import ipaddress
import math
import os
from pathlib import Path
import stat
import sys
from urllib.parse import urlsplit, urlunsplit


class UploadError(Exception):
    """The OTA endpoint did not confirm the update."""


def parse_host(value):
    """Accept a hostname/IP, optional port, or HTTP(S) origin."""
    if "://" not in value:
        try:
            ipaddress.IPv6Address(value)
        except ValueError:
            pass
        else:
            value = "[" + value + "]"
        value = "http://" + value
    try:
        target = urlsplit(value)
        port = target.port  # Validate port syntax and range before any upload.
        if target.scheme not in ("http", "https") or not target.hostname:
            raise ValueError("expected a hostname/IP or HTTP(S) origin")
        if port == 0:
            raise ValueError("port must be between 1 and 65535")
        if target.username is not None or target.password is not None:
            raise ValueError("use --username/--password for HTTP Basic authentication")
        if target.path not in ("", "/") or target.query or target.fragment:
            raise ValueError("provide only the host; /ota-upload is added automatically")
        if any(char.isspace() or ord(char) < 32 for char in value):
            raise ValueError("host must not contain whitespace or control characters")
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    return target


def positive_timeout(value):
    try:
        seconds = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("timeout must be a positive number") from exc
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be a finite positive number")
    return seconds


def upload_firmware(target, firmware, timeout=120, username=None, password=None):
    with firmware.open("rb") as image:
        info = os.fstat(image.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size == 0:
            raise UploadError("firmware must be a nonempty regular application .bin file")
        headers = {
            "Content-Type": "application/octet-stream",
            "Content-Length": str(info.st_size),
            "Connection": "close",
        }
        if username is not None:
            credentials = "{}:{}".format(username, password or "").encode("utf-8")
            headers["Authorization"] = "Basic " + base64.b64encode(credentials).decode("ascii")

        connection_type = (http.client.HTTPSConnection if target.scheme == "https"
                           else http.client.HTTPConnection)
        connection = connection_type(target.hostname, target.port, timeout=timeout)
        url = urlunsplit((target.scheme, target.netloc, "/ota-upload", "", ""))
        print("Uploading {} ({:,} bytes) to {} ...".format(firmware, info.st_size, url),
              flush=True)
        try:
            # Explicit length prevents chunked encoding; the file is streamed.
            connection.request("POST", "/ota-upload", body=image, headers=headers)
            response = connection.getresponse()
            body = response.read()
            if response.status != 200 or body != b"OK":
                detail = body.decode("utf-8", errors="replace").strip()
                raise UploadError("update not confirmed: HTTP {} {}{}".format(
                    response.status, response.reason, ": " + detail if detail else ""))
        except (OSError, http.client.HTTPException) as exc:
            raise UploadError("update not confirmed: {}. The MCU may have received the "
                              "image; check its state before retrying".format(exc)) from exc
        finally:
            connection.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", type=parse_host, help="hostname/IP, optionally with :port or http(s)://")
    parser.add_argument("firmware", type=Path, help="application .bin file (not a merged flash image)")
    parser.add_argument("--timeout", type=positive_timeout, default=120,
                        help="socket timeout in seconds (default: 120)")
    parser.add_argument("--username", help="HTTP Basic auth username; prompts for password if omitted")
    parser.add_argument("--password", help="HTTP Basic auth password (visible in command-line history)")
    args = parser.parse_args(argv)
    if args.password is not None and args.username is None:
        parser.error("--password requires --username")
    if args.username is not None and ":" in args.username:
        parser.error("HTTP Basic auth username must not contain ':'")
    try:
        password = args.password
        if args.username is not None and password is None:
            password = getpass.getpass("HTTP password: ")
        upload_firmware(args.host, args.firmware.expanduser(), args.timeout,
                        args.username, password)
    except (UploadError, OSError, EOFError) as exc:
        print("Error: {}".format(exc), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nUpload interrupted; check the MCU's state before retrying.", file=sys.stderr)
        return 130
    print("OTA update accepted. The MCU will reboot automatically.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
