BUILD_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

.PHONY: all clean

all: build/btrfs-backup-profile

build/btrfs-backup-profile: CMakeLists.txt $(shell find cpp -type f | sort)
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel $(BUILD_JOBS)

clean:
	rm -rf build
