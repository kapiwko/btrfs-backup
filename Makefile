# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

BUILD_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CMAKE_CONFIGURE_ARGS ?=

.PHONY: all test check-format clang-tidy clang-tidy-changed quality quality-changed clean

all:
	cmake --preset default $(CMAKE_CONFIGURE_ARGS)
	cmake --build --preset default --parallel $(BUILD_JOBS)

test: all
	ctest --preset default --parallel $(BUILD_JOBS)

check-format:
	./tools/check-cpp-format.sh

clang-tidy:
	BUILD_JOBS=$(BUILD_JOBS) ./tools/run-clang-tidy.sh

clang-tidy-changed:
	BUILD_JOBS=$(BUILD_JOBS) ./tools/run-clang-tidy-changed.sh

quality: check-format clang-tidy

quality-changed: check-format clang-tidy-changed

clean:
	rm -rf build
