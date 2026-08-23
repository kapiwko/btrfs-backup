#include <btrfsbackup/command/state_command.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <btrfsbackup/command/state_fingerprint_command.hpp>
#include <btrfsbackup/command/state_run_command.hpp>

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl state: " << message << '\n';
    std::exit(code);
}

void usage() {
    std::cout << "Usage: btrfs-backupctl state COMMAND\n"
              << "\nCommands:\n"
              << "  fingerprint [OPTIONS]\n"
              << "  check-last-success [OPTIONS]\n"
              << "  write-success [OPTIONS]\n"
              << "  pending write|read|clear [OPTIONS]\n";
}

int pending(const std::vector<std::string>& args) {
    if (args.empty()) {
        usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "write") {
        btrfsbackup::command::state_pending_write(rest);
        return 0;
    }
    if (command == "read") {
        btrfsbackup::command::state_pending_read(rest, std::cout);
        return 0;
    }
    if (command == "clear") {
        btrfsbackup::command::state_pending_clear(rest);
        return 0;
    }
    fail("unknown pending command: " + command);
}

} // namespace

namespace btrfsbackup::command {

int state(const std::vector<std::string>& args) {
    if (args.empty()) {
        usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "fingerprint") {
        state_fingerprint(rest, std::cout);
        return 0;
    }
    if (command == "check-last-success") {
        state_check_last_success(rest, std::cout);
        return 0;
    }
    if (command == "write-success") {
        state_write_success(rest);
        return 0;
    }
    if (command == "pending") {
        return pending(rest);
    }
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    fail("unknown command: " + command);
}

} // namespace btrfsbackup::command
