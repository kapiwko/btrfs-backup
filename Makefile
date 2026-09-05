# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

BUILD_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CMAKE_CONFIGURE_ARGS ?=

.PHONY: all test check-format clang-tidy clang-tidy-changed quality quality-changed manual-lab clean

all:
	cmake --preset default $(CMAKE_CONFIGURE_ARGS)
	cmake --build --preset default --parallel $(BUILD_JOBS)

test: all
	ctest --preset default --parallel $(BUILD_JOBS)

check-format:
	cmake --preset default
	cmake --build --preset default --target check-format

clang-tidy:
	cmake --preset clang-tidy
	cmake --build --preset clang-tidy --parallel $(BUILD_JOBS)

clang-tidy-changed:
	BUILD_JOBS=$(BUILD_JOBS) ./tools/run_clang_tidy_changed.py

quality: check-format clang-tidy

quality-changed: check-format clang-tidy-changed

manual-lab:
	cmake --preset kde-debug $(CMAKE_CONFIGURE_ARGS)
	cmake --build --preset kde-debug --target manual-kde-lab --parallel $(BUILD_JOBS)

clean:
	rm -rf build
