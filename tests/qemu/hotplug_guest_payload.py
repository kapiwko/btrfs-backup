#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
from pathlib import Path


PAYLOAD = Path(__file__).with_name("hotplug_guest_setup.sh")


def render_guest_setup(target_uuid: str, target_device_unit: str, replacement_hash: str) -> str:
    return (
        PAYLOAD.read_text()
        .replace("@TARGET_UUID@", target_uuid)
        .replace("@TARGET_DEVICE_UNIT@", target_device_unit)
        .replace("@REPLACEMENT_HASH@", replacement_hash)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Render the QEMU guest provisioning payload.")
    parser.add_argument("--target-uuid", required=True)
    parser.add_argument("--target-device-unit", required=True)
    parser.add_argument("--replacement-hash", required=True)
    arguments = parser.parse_args()
    print(render_guest_setup(arguments.target_uuid, arguments.target_device_unit, arguments.replacement_hash), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
