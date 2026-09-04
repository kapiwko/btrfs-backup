#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import subprocess

from hotplug_guest_payload import render_guest_setup


def main() -> int:
    rendered = render_guest_setup("test-uuid", "test.device", "deadbeef")
    for placeholder in ("@TARGET_UUID@", "@TARGET_DEVICE_UNIT@", "@REPLACEMENT_HASH@"):
        if placeholder in rendered:
            raise AssertionError(f"unresolved guest payload placeholder: {placeholder}")
    lines = rendered.splitlines()
    install = lines.index("install -d -m0755 " + chr(92))
    if not all(lines[index].endswith(chr(92)) for index in range(install, install + 3)):
        raise AssertionError("guest setup lost its directory-list continuations")
    subprocess.run(["bash", "-n"], input=rendered, text=True, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
