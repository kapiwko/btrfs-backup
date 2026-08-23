.PHONY: all clean

all: build/btrfs-backup-profile

build/btrfs-backup-profile: CMakeLists.txt cpp/apps/btrfs-backup-profile.cpp cpp/src/profile_tool.cpp cpp/include/btrfsbackup/profile_tool.hpp
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --target btrfs-backup-profile

clean:
	rm -rf build
