CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=

.PHONY: all clean

all: build/btrfs-backup-profile

build/btrfs-backup-profile: cpp/apps/btrfs-backup-profile.cpp
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf build
