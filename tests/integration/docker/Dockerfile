# syntax=docker/dockerfile:1

FROM archlinux:base-devel

# The privileged integration tests build and install only the base package.
RUN pacman -Syu --noconfirm --needed \
        bash \
        btrfs-progs \
        cmake \
        coreutils \
        cryptsetup \
        diffutils \
        findutils \
        gawk \
        grep \
        libarchive \
        nlohmann-json \
        sed \
        systemd \
        util-linux \
        zstd \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
