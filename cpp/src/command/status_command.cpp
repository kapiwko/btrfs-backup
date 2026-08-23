#include <btrfsbackup/command/status_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/command/status_history_command.hpp>
#include <btrfsbackup/command/status_show_command.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/command/status_write_command.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl status: " << message << '\n';
    std::exit(code);
}

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        fail("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

void watch(const fs::path& status_root, const std::vector<std::string>& args) {
    std::string profile = "default";
    double interval = 1.0;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--interval" && i + 1 < args.size()) {
            interval = std::stod(args[++i]);
            if (interval <= 0) {
                fail("--interval must be greater than zero");
            }
        } else {
            fail("unknown watch option: " + arg);
        }
    }

    btrfsbackup::validate_profile_id(profile);
    fs::path path = status_root / profile / "current.json";
    std::string previous;
    while (true) {
        if (readable_file(path)) {
            std::string current = read_text_file(path);
            if (current != previous) {
                std::cout << current;
                if (current.empty() || current.back() != '\n') {
                    std::cout << '\n';
                }
                std::cout.flush();
                previous = std::move(current);
            }
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
}

void usage() {
    std::cout << "Usage: btrfs-backupctl status COMMAND\n"
              << "\nCommands:\n"
              << "  show [--profile ID|--all] [--human]\n"
              << "  history [--profile ID] [--limit N]\n"
              << "  watch [--profile ID] [--interval SECONDS]\n"
              << "  write [OPTIONS]\n";
}

} // namespace

namespace btrfsbackup::command {

int status(const fs::path& status_root, const fs::path& history_root, const std::vector<std::string>& args) {
    if (args.empty()) {
        usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "show") {
        status_show(status_root, history_root, rest, std::cout);
        return 0;
    }
    if (command == "history") {
        status_history(history_root, rest, std::cout);
        return 0;
    }
    if (command == "watch") {
        watch(status_root, rest);
        return 0;
    }
    if (command == "write") {
        status_write(status_root, history_root, rest);
        return 0;
    }
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    fail("unknown command: " + command);
}

} // namespace btrfsbackup::command
