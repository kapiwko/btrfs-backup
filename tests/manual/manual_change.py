#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import random


destination = Path("/srv/manual-source/home/tester/Large cancellation file.bin")
print(f"Writing new contents to {destination}…", flush=True)
with destination.open("wb") as stream:
    generator = random.Random(20260906)
    for _ in range(1536):
        stream.write(generator.randbytes(1024 * 1024))
print("Ready. Start a backup from the widget and cancel it during transfer.")
input("Press Enter to close this window…")
