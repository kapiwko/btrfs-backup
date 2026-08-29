# syntax=docker/dockerfile:1

FROM archlinux:base-devel

# The Arch release target builds both the base and Plasma packages.
RUN pacman -Syu --noconfirm --needed \
        bash \
        btrfs-progs \
        cmake \
        coreutils \
        cryptsetup \
        diffutils \
        extra-cmake-modules \
        findutils \
        gawk \
        grep \
        ki18n \
        kirigami \
        kpackage \
        libarchive \
        libplasma \
        nlohmann-json \
        qt6-base \
        qt6-declarative \
        sed \
        systemd \
        util-linux \
        zstd \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
