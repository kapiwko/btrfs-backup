# syntax=docker/dockerfile:1

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

FROM archlinux:base-devel

RUN pacman -Syu --noconfirm --needed \
        btrfs-progs \
        cmake \
        dbus \
        extra-cmake-modules \
        imagemagick \
        kactivitymanagerd \
        kcmutils \
        kcoreaddons \
        ki18n \
        kio \
        kirigami \
        kjobwidgets \
        kpackage \
        krunner \
        kwin \
        libplasma \
        ninja \
        nlohmann-json \
        plasma-desktop \
        plasma-workspace \
        python-gobject \
        qt6-base \
        qt6-declarative \
        spectacle \
        systemd \
        systemsettings \
        util-linux \
        xorg-xwayland \
    && rm -rf /var/cache/pacman/pkg/*

RUN pacman -S --noconfirm --needed breeze dolphin xdotool xorg-server-xvfb \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /workspace
