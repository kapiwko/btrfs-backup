#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import json
import socket
import time
from pathlib import Path
from typing import Any


class QmpError(RuntimeError):
    pass


class QmpClient:
    def __init__(self, socket_path: Path, timeout: float = 5.0) -> None:
        self.socket_path = socket_path
        self.timeout = timeout

    def execute(self, name: str, arguments: dict[str, Any] | None = None) -> None:
        deadline = time.monotonic() + self.timeout
        while not self.socket_path.exists():
            if time.monotonic() >= deadline:
                raise QmpError(f"QMP socket did not appear: {self.socket_path}")
            time.sleep(0.05)

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(self.timeout)
            connection.connect(str(self.socket_path))
            stream = connection.makefile("rwb", buffering=0)
            self._read_response(stream)
            self._send(stream, {"execute": "qmp_capabilities"})
            self._read_response(stream)
            request: dict[str, Any] = {"execute": name}
            if arguments is not None:
                request["arguments"] = arguments
            self._send(stream, request)
            self._read_response(stream)

    @staticmethod
    def _send(stream: Any, request: dict[str, Any]) -> None:
        stream.write(json.dumps(request, separators=(",", ":")).encode() + b"\n")

    @staticmethod
    def _read_response(stream: Any) -> None:
        while line := stream.readline():
            message = json.loads(line)
            if "error" in message:
                raise QmpError(str(message["error"]))
            if "return" in message or "QMP" in message:
                return
        raise QmpError("QMP connection closed without a response")
