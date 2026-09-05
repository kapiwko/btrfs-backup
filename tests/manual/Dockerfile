# syntax=docker/dockerfile:1

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

FROM archlinux:base

RUN pacman -Syu --noconfirm --needed \
        btrfs-progs \
        breeze \
        cryptsetup \
        dbus \
        dolphin \
        e2fsprogs \
        konsole \
        linux \
        noto-fonts \
        noto-fonts-emoji \
        plasma-desktop \
        plasma-workspace \
        polkit \
        python \
        qemu-img \
        qemu-system-x86 \
        sddm \
        systemsettings \
        util-linux \
        xorg-server \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
