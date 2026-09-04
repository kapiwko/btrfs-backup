// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerTestClient.hpp"

#include <core/ManagerProtocol.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace btrfsbackup::integration {

namespace {

using Json = nlohmann::json;
using platform::linux::OwnedFileDescriptor;

constexpr std::chrono::minutes preparation_timeout{3};
constexpr std::chrono::milliseconds polling_interval{100};

enum class ExitCode : int {
    Success = 0,
    Failure = 1,
    InvalidArguments = 2,
    Timeout = 6,
};

struct ClientOptions {
    std::filesystem::path target;
    std::string source;
    std::filesystem::path passphrase_file;
    std::string mode{"reformat-existing-partition"};
    std::string profile_id{"partition-integration"};
    bool start_only{false};
};

struct BlockDeviceGeometry {
    std::uint64_t size_bytes{};
    std::uint32_t logical_sector_size{};
};

[[nodiscard]] Json parse_document(
    const std::string& payload,
    int expected_schema_version,
    std::string_view description
) {
    const Json document = Json::parse(payload);
    if (!document.is_object() || document.at("schemaVersion").get<int>() != expected_schema_version)
        throw std::runtime_error(std::string("manager returned an incompatible ") + std::string(description));
    return document;
}

[[nodiscard]] std::string required_nonempty_string(
    const Json& document,
    std::string_view field,
    std::string_view description
) {
    const std::string value = document.at(std::string(field)).get<std::string>();
    if (value.empty())
        throw std::runtime_error(std::string("manager omitted ") + std::string(description));
    return value;
}

[[nodiscard]] long partition_number_from_path(std::string_view path) {
    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
    const auto first_digit = std::find_if_not(path.rbegin(), path.rend(), digit).base();
    if (first_digit == path.end())
        return -1;
    long result = -1;
    const auto parsed = std::from_chars(first_digit, path.end(), result);
    return parsed.ec == std::errc{} && parsed.ptr == path.end() ? result : -1;
}

[[nodiscard]] BlockDeviceGeometry read_block_device_geometry(const std::filesystem::path& path) {
    const OwnedFileDescriptor descriptor(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        throw std::runtime_error("cannot open the selected block device");
    std::uint64_t size_bytes = 0;
    unsigned int logical_sector_size = 0;
    const bool valid = ::ioctl(descriptor.get(), BLKGETSIZE64, &size_bytes) == 0 &&
        ::ioctl(descriptor.get(), BLKSSZGET, &logical_sector_size) == 0 && logical_sector_size != 0;
    if (!valid)
        throw std::runtime_error("cannot read the selected block device geometry");
    return {.size_bytes = size_bytes, .logical_sector_size = logical_sector_size};
}

[[nodiscard]] bool region_matches_geometry(const Json& region, const BlockDeviceGeometry& geometry) {
    const std::uint64_t sectors = region.at("sectorCount").get<std::uint64_t>();
    return sectors <= std::numeric_limits<std::uint64_t>::max() / geometry.logical_sector_size &&
        sectors * geometry.logical_sector_size == geometry.size_bytes;
}

[[nodiscard]] std::string select_partition_candidate(
    const Json& topology,
    long expected_partition_number,
    const BlockDeviceGeometry& geometry
) {
    for (const auto& device : topology.at("devices")) {
        for (const auto& region : device.at("regions")) {
            if (region.at("kind").get<std::string>() == "existing-partition" &&
                region.at("partitionNumber").get<long>() == expected_partition_number &&
                region_matches_geometry(region, geometry) && region.at("suitableForReformat").get<bool>()) {
                return required_nonempty_string(region, "candidateId", "storage candidate identifier");
            }
        }
    }
    throw std::runtime_error("selected storage target is absent from storage topology");
}

[[nodiscard]] std::string select_unallocated_candidate(
    const Json& topology,
    const BlockDeviceGeometry& geometry
) {
    for (const auto& device : topology.at("devices")) {
        if (device.at("sizeBytes").get<std::uint64_t>() != geometry.size_bytes)
            continue;
        for (const auto& region : device.at("regions")) {
            if (region.at("kind").get<std::string>() == "unallocated" &&
                region.at("suitableForBackupPartition").get<bool>()) {
                return required_nonempty_string(region, "candidateId", "storage candidate identifier");
            }
        }
    }
    throw std::runtime_error("selected storage target is absent from storage topology");
}

[[nodiscard]] std::string select_device_candidate(
    const Json& topology,
    const BlockDeviceGeometry& geometry
) {
    for (const auto& device : topology.at("devices")) {
        if (device.at("sizeBytes").get<std::uint64_t>() == geometry.size_bytes)
            return required_nonempty_string(device, "candidateId", "storage candidate identifier");
    }
    throw std::runtime_error("selected storage target is absent from storage topology");
}

[[nodiscard]] std::string select_source_candidate(const Json& candidates, std::string_view source) {
    if (!candidates.is_array())
        throw std::runtime_error("manager returned an invalid source candidate document");
    for (const auto& candidate : candidates) {
        if (candidate.at("path").get<std::string>() == source)
            return required_nonempty_string(candidate, "id", "source candidate identifier");
    }
    throw std::runtime_error("selected source is absent from source candidates");
}

[[nodiscard]] OwnedFileDescriptor open_secret(const std::filesystem::path& path) {
    if (path == "-") {
        OwnedFileDescriptor descriptor(::fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3));
        if (!descriptor.valid())
            throw std::runtime_error("cannot duplicate the passphrase descriptor");
        return descriptor;
    }
    OwnedFileDescriptor descriptor(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        throw std::runtime_error("cannot open the passphrase file");
    return descriptor;
}

} // namespace

class DeviceProvisioningClient final {
  public:
    int run(const ClientOptions& options) {
        const Json topology = inspect_topology();
        const auto geometry = read_block_device_geometry(options.target);
        const std::string candidate = select_candidate(options, topology, geometry);
        const std::string generation =
            required_nonempty_string(topology, "generation", "storage topology generation");
        const std::string inspection_id = inspect_existing_target(options, generation, candidate);
        const std::string plan_id = build_plan(options.mode, generation, candidate, inspection_id);
        const std::string source_id = list_source_candidate(options.source);
        const auto [operation_id, initial_status] = start(options, plan_id, source_id);
        if (options.start_only) {
            std::cout << initial_status << '\n';
            return static_cast<int>(ExitCode::Success);
        }
        return wait_for_completion(operation_id);
    }

    int delete_profile(const std::string& profile_id) {
        const Json details = parse_document(
            manager_.call(manager_protocol::method::get_profile_details, profile_id),
            manager_protocol::profile_details_schema_version,
            "profile details"
        );
        const std::string generation =
            required_nonempty_string(details, "configurationGeneration", "configuration generation");
        const std::string fingerprint =
            required_nonempty_string(details, "configurationFingerprint", "configuration fingerprint");
        const Json deleted = parse_document(
            manager_.call(manager_protocol::method::delete_profile, profile_id, generation, fingerprint),
            manager_protocol::operation_result_schema_version,
            "profile deletion result"
        );
        if (!deleted.at("accepted").get<bool>() || deleted.at("profileId").get<std::string>() != profile_id)
            throw std::runtime_error("manager did not confirm profile deletion");
        std::cout << deleted.dump() << '\n';
        return static_cast<int>(ExitCode::Success);
    }

  private:
    [[nodiscard]] Json inspect_topology() const {
        const std::string payload = manager_.call(manager_protocol::method::inspect_storage_topology);
        try {
            return parse_document(payload, manager_protocol::storage_topology_schema_version, "storage topology");
        } catch (...) {
            std::cerr << "device provisioning client: storage topology: " << payload << '\n';
            throw;
        }
    }

    [[nodiscard]] static std::string select_candidate(
        const ClientOptions& options,
        const Json& topology,
        const BlockDeviceGeometry& geometry
    ) {
        if (options.mode == "erase-whole-device")
            return select_device_candidate(topology, geometry);
        if (options.mode == "create-partition-in-unallocated-space")
            return select_unallocated_candidate(topology, geometry);
        return select_partition_candidate(topology, partition_number_from_path(options.target.string()), geometry);
    }

    [[nodiscard]] std::string inspect_existing_target(
        const ClientOptions& options,
        const std::string& generation,
        const std::string& candidate
    ) const {
        if (options.mode != "adopt-existing-target")
            return {};
        const std::string request = Json{{"topologyGeneration", generation}, {"candidateId", candidate}}.dump();
        const OwnedFileDescriptor descriptor = open_secret(options.passphrase_file);
        const std::string payload =
            manager_.call_with_fd(manager_protocol::method::inspect_existing_target, request, descriptor.get());
        const Json inspection = parse_document(
            payload,
            manager_protocol::existing_target_inspection_schema_version,
            "existing target inspection"
        );
        if (inspection.at("classification").get<std::string>() != "compatible-repository")
            throw std::runtime_error("selected storage target is not an adoptable repository");
        return required_nonempty_string(inspection, "inspectionId", "target inspection identifier");
    }

    [[nodiscard]] std::string build_plan(
        const std::string& mode,
        const std::string& generation,
        const std::string& candidate,
        const std::string& inspection_id
    ) const {
        const std::string request = Json{
            {"topologyGeneration", generation},
            {"candidateId", candidate},
            {"mode", mode},
            {"inspectionId", inspection_id},
        }
                                        .dump();
        const Json plan = parse_document(
            manager_.call(manager_protocol::method::build_device_preparation_plan, request),
            manager_protocol::device_preparation_plan_schema_version,
            "device preparation plan"
        );
        return required_nonempty_string(plan, "planId", "preparation plan identifier");
    }

    [[nodiscard]] std::string list_source_candidate(std::string_view source) const {
        const std::string payload = manager_.call(manager_protocol::method::list_source_candidates);
        try {
            return select_source_candidate(Json::parse(payload), source);
        } catch (...) {
            std::cerr << "device provisioning client: source candidates: " << payload << '\n';
            throw;
        }
    }

    [[nodiscard]] std::pair<std::string, std::string> start(
        const ClientOptions& options,
        const std::string& plan_id,
        const std::string& source_id
    ) const {
        const std::string request = Json{
            {"profileId", options.profile_id},
            {"profileName", "Partition integration"},
            {"planId", plan_id},
            {"sourceCandidateId", source_id},
            {"passphraseLabel", "Integration"},
            {"createAutomaticKey", false},
        }
                                        .dump();
        const OwnedFileDescriptor descriptor = open_secret(options.passphrase_file);
        const std::string payload =
            manager_.call_with_fd(manager_protocol::method::start_device_preparation, request, descriptor.get());
        const Json status = parse_status(payload);
        return {required_nonempty_string(status, "operationId", "preparation operation identifier"), payload};
    }

    [[nodiscard]] static Json parse_status(const std::string& payload) {
        return parse_document(
            payload,
            manager_protocol::device_provisioning_schema_version,
            "device preparation status"
        );
    }

    int wait_for_completion(const std::string& operation_id) const {
        const auto deadline = std::chrono::steady_clock::now() + preparation_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            const std::string payload =
                manager_.call(manager_protocol::method::get_device_preparation, operation_id);
            const std::string state = parse_status(payload).at("state").get<std::string>();
            if (state == "succeeded") {
                std::cout << payload << '\n';
                return static_cast<int>(ExitCode::Success);
            }
            if (state == "failed" || state == "cancelled" || state == "interrupted") {
                std::cerr << payload << '\n';
                throw std::runtime_error("device preparation did not succeed");
            }
            if (state != "queued" && state != "running")
                throw std::runtime_error("manager returned an unknown device preparation state");
            std::this_thread::sleep_for(polling_interval);
        }
        std::cerr << "device provisioning client: device preparation timed out\n";
        return static_cast<int>(ExitCode::Timeout);
    }

    ManagerTestClient manager_;
};

namespace {

[[nodiscard]] std::optional<ClientOptions> parse_options(int argc, char** argv) {
    if (argc < 4 || argc > 7)
        return std::nullopt;
    ClientOptions options{.target = argv[1], .source = argv[2], .passphrase_file = argv[3]};
    if (argc >= 5)
        options.mode = argv[4];
    if (argc >= 6)
        options.profile_id = argv[5];
    if (argc == 7) {
        if (std::string_view(argv[6]) != "start-only")
            return std::nullopt;
        options.start_only = true;
    }
    constexpr std::string_view supported_modes[] = {
        "reformat-existing-partition",
        "create-partition-in-unallocated-space",
        "erase-whole-device",
        "adopt-existing-target",
    };
    if (std::find(std::begin(supported_modes), std::end(supported_modes), options.mode) ==
        std::end(supported_modes)) {
        return std::nullopt;
    }
    return options;
}

} // namespace

} // namespace btrfsbackup::integration

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--delete-profile") {
        try {
            return btrfsbackup::integration::DeviceProvisioningClient{}.delete_profile(argv[2]);
        } catch (const std::exception& error) {
            std::cerr << "device provisioning client: " << error.what() << '\n';
            return 1;
        }
    }
    const auto options = btrfsbackup::integration::parse_options(argc, argv);
    if (!options) {
        std::cerr << "usage: " << argv[0]
                  << " TARGET SOURCE PASSPHRASE_FILE_OR_DASH [MODE [PROFILE_ID [start-only]]]\n"
                  << "       " << argv[0] << " --delete-profile PROFILE_ID\n";
        return 2;
    }
    try {
        return btrfsbackup::integration::DeviceProvisioningClient{}.run(*options);
    } catch (const std::exception& error) {
        std::cerr << "device provisioning client: " << error.what() << '\n';
        return 1;
    }
}
