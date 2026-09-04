// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerTestClient.hpp"

#include <core/ManagerProtocol.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace btrfsbackup::integration {

class BrowseSessionClient final {
  public:
    int run(const std::string& profile_id, const std::filesystem::path& hold_file) {
        const std::string payload = manager_.call(manager_protocol::method::open_browse_session, profile_id);
        validate_session(payload, profile_id);
        std::cout << payload << '\n'
                  << std::flush;

        std::error_code error;
        while (std::filesystem::exists(hold_file, error) && !error) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 0;
    }

  private:
    static void validate_session(const std::string& payload, const std::string& expected_profile_id) {
        const auto document = nlohmann::json::parse(payload);
        if (!document.is_object() ||
            document.at("schemaVersion").get<int>() != manager_protocol::browse_session_schema_version ||
            document.at("sessionId").get<std::string>().empty() ||
            document.at("profileId").get<std::string>() != expected_profile_id ||
            document.at("rootPath").get<std::string>().empty() ||
            document.at("expiresAt").get<std::string>().empty() ||
            !document.at("readOnly").get<bool>()) {
            throw std::runtime_error("manager returned an invalid browse session document");
        }
    }

    ManagerTestClient manager_;
};

} // namespace btrfsbackup::integration

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " PROFILE HOLD_FILE\n";
        return 2;
    }
    try {
        return btrfsbackup::integration::BrowseSessionClient{}.run(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::cerr << "browse session D-Bus call failed: " << error.what() << '\n';
        return 1;
    }
}
