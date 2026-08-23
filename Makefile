.PHONY: all clean

all: build/btrfs-backup-profile

build/btrfs-backup-profile: CMakeLists.txt $(shell find cpp -type f | sort)
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --target btrfs-backup-profile

clean:
	rm -rf build
