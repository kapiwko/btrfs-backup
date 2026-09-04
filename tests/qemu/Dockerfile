# syntax=docker/dockerfile:1

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

ARG BASE_IMAGE=btrfs-backup-real-test:local
FROM ${BASE_IMAGE}

RUN pacman -Syu --noconfirm --needed \
        e2fsprogs \
        linux \
        polkit \
        qemu-system-x86 \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
