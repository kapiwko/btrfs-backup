FROM archlinux:base-devel

RUN pacman -Syu --noconfirm --needed \
        bash \
        btrfs-progs \
        coreutils \
        cryptsetup \
        diffutils \
        findutils \
        gawk \
        grep \
        libarchive \
        sed \
        systemd \
        util-linux \
        zstd \
    && rm -rf /var/cache/pacman/pkg/*

WORKDIR /work
