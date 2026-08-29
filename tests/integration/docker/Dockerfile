# syntax=docker/dockerfile:1

FROM archlinux:base

# The privileged integration tests build and install only the base package.
RUN pacman -Syu --noconfirm --needed \
        bash \
        btrfs-progs \
        coreutils \
        cryptsetup \
        diffutils \
        findutils \
        gawk \
        grep \
        perl \
        sed \
        systemd \
        util-linux \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
