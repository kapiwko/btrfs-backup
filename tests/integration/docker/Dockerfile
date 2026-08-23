FROM archlinux:base-devel

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
