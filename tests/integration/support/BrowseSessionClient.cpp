// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerTestClient.hpp"

#include <core/ManagerProtocol.hpp>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::integration {

class BrowseSessionClient final {
  public:
    int run_expect_denied(const std::string& profile_id) {
        try {
            static_cast<void>(manager_->call(manager_protocol::method::open_browse_session, profile_id));
        } catch (const std::exception& error) {
            if (std::string_view(error.what()).contains("not authorized"))
                return 0;
            throw;
        }
        throw std::runtime_error("unprivileged caller unexpectedly opened a browse session");
    }

    int run(const std::string& profile_id, const std::filesystem::path& hold_file) {
        const std::string payload = manager_->call(manager_protocol::method::open_browse_session, profile_id);
        validate_session(payload, profile_id);
        std::cout << payload << '\n'
                  << std::flush;

        std::error_code error;
        while (std::filesystem::exists(hold_file, error) && !error) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 0;
    }

    int run_self_check(const std::string& profile_id, const std::filesystem::path& target_mount) {
        std::filesystem::path browse_root;
        {
            const std::string payload = manager_->call(manager_protocol::method::open_browse_session, profile_id);
            validate_session(payload, profile_id);
            const std::string session_id = nlohmann::json::parse(payload).at("sessionId").get<std::string>();
            browse_root = std::filesystem::path{"/run/btrfs-backup-browse"} /
                std::to_string(getuid()) / session_id / "repository";
            if (!browse_root.string().starts_with("/run/btrfs-backup-browse/") ||
                browse_root.filename() != "repository")
                throw std::runtime_error("test derived an invalid browse root");
            btrfsbackup::platform::linux::OwnedFileDescriptor root(
                manager_->call_for_fd(manager_protocol::method::open_browse_root, session_id)
            );
            struct stat root_status{};
            if (!root.valid() || fstat(root.get(), &root_status) != 0 || !S_ISDIR(root_status.st_mode))
                throw std::runtime_error("manager did not return a pinned browse root directory");
            const auto options = mount_options(browse_root);
            for (const std::string_view required : {"ro", "nodev", "nosuid", "noexec", "nosymfollow"})
                if (!options.contains(std::string(required)))
                    throw std::runtime_error("browse mount omitted option " + std::string(required));
            btrfsbackup::platform::linux::OwnedFileDescriptor probe(
                openat(root.get(), "browse-probe.txt", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
            );
            char content[32]{};
            const ssize_t length = probe.valid() ? read(probe.get(), content, sizeof(content)) : -1;
            if (length != 13 || std::string_view(content, static_cast<std::size_t>(length)) != "browse probe\n")
                throw std::runtime_error("browse session did not expose repository data");
            const int writable = openat(root.get(), "browse-probe.txt", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
            if (writable >= 0) {
                close(writable);
                throw std::runtime_error("browse session unexpectedly permits writes");
            }
        }
        manager_.reset();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (find_mount_options(browse_root).has_value() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (find_mount_options(browse_root).has_value())
            throw std::runtime_error("browse session survived D-Bus caller disconnect");
        static_cast<void>(mount_options(target_mount));
        return 0;
    }

  private:
    static std::optional<std::set<std::string>> find_mount_options(const std::filesystem::path& mount_point) {
        std::ifstream input("/proc/self/mountinfo");
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream fields(line);
            std::string value;
            std::string mounted_at;
            std::string options;
            for (int column = 1; column <= 6 && fields >> value; ++column) {
                if (column == 5)
                    mounted_at = value;
                if (column == 6)
                    options = value;
            }
            if (mounted_at != mount_point.string())
                continue;
            std::set<std::string> result;
            std::istringstream option_stream(options);
            while (std::getline(option_stream, value, ','))
                result.insert(value);
            return result;
        }
        return std::nullopt;
    }

    static std::set<std::string> mount_options(const std::filesystem::path& mount_point) {
        if (const auto result = find_mount_options(mount_point); result.has_value())
            return *result;
        throw std::runtime_error("mount is absent from mountinfo: " + mount_point.string());
    }

    static void validate_session(const std::string& payload, const std::string& expected_profile_id) {
        const auto document = nlohmann::json::parse(payload);
        if (!document.is_object() ||
            document.at("schemaVersion").get<int>() != manager_protocol::browse_session_schema_version ||
            document.at("sessionId").get<std::string>().empty() ||
            document.at("profileId").get<std::string>() != expected_profile_id ||
            document.at("expiresAt").get<std::string>().empty() ||
            !document.at("readOnly").get<bool>()) {
            throw std::runtime_error("manager returned an invalid browse session document");
        }
        if (document.contains("rootPath"))
            throw std::runtime_error("manager exposed a local browse root path");
    }

    std::unique_ptr<ManagerTestClient> manager_{std::make_unique<ManagerTestClient>()};
};

} // namespace btrfsbackup::integration

int main(int argc, char** argv) {
    const bool self_check = argc == 4 && std::string_view(argv[2]) == "--self-check";
    const bool expect_denied = argc == 3 && std::string_view(argv[2]) == "--expect-denied";
    if (argc != 3 && !self_check) {
        std::cerr << "usage: " << argv[0] << " PROFILE HOLD_FILE\n"
                  << "       " << argv[0] << " PROFILE --expect-denied\n"
                  << "       " << argv[0] << " PROFILE --self-check TARGET_MOUNT\n";
        return 2;
    }
    try {
        if (expect_denied)
            return btrfsbackup::integration::BrowseSessionClient{}.run_expect_denied(argv[1]);
        if (self_check)
            return btrfsbackup::integration::BrowseSessionClient{}.run_self_check(argv[1], argv[3]);
        return btrfsbackup::integration::BrowseSessionClient{}.run(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::cerr << "browse session D-Bus call failed: " << error.what() << '\n';
        return 1;
    }
}
