# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

BUILD_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CMAKE_CONFIGURE_ARGS ?=

.PHONY: all check-format clang-tidy quality clean

all: build/btrfs-backupctl

build/btrfs-backupctl: CMakeLists.txt $(shell find apps src -type f | sort)
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release $(CMAKE_CONFIGURE_ARGS)
	cmake --build build --parallel $(BUILD_JOBS)

check-format:
	./tools/check-cpp-format.sh

clang-tidy:
	BUILD_JOBS=$(BUILD_JOBS) ./tools/run-clang-tidy.sh

quality: check-format clang-tidy

clean:
	rm -rf build
