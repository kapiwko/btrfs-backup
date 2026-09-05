#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

import subprocess
import sys
import time


def click(x, y):
    subprocess.run(
        ["xdotool", "mousemove", str(x), str(y), "click", "1"],
        check=True,
    )
    time.sleep(2)


page = sys.argv[1] if len(sys.argv) > 1 else ""
time.sleep(5)

if page.startswith("new-profile"):
    click(900, 95)
    if page.startswith("new-profile-adopt"):
        click(540, 455)
        if page.endswith("-partition"):
            click(540, 215)
            click(600, 290)
    elif page.startswith("new-profile-prepare"):
        click(540, 525)
        if page.endswith("-partition"):
            click(540, 275)
            click(600, 360)
elif page in {"profile-details", "profile-settings", "history"}:
    click(390, 145)
    if page == "profile-settings":
        click(800, 95)
    elif page == "history":
        click(680, 95)
elif page == "notification-settings":
    click(380, 700)
