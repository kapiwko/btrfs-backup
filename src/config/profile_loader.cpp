#include <config/profile_loader.hpp>

#include <filesystem>
#include <string>

#include <config/errors.hpp>
#include <config/identifiers.hpp>
#include <platform/linux/trusted_file.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

fs::path profile_json_path(const fs::path& etc_root, const std::string& profile_id) {
    validate_identifier(profile_id, "profile");
    return etc_root / "profiles" / profile_id / "profile.json";
}

Json load_profile_json_by_id(const fs::path& etc_root, const std::string& profile_id) {
    fs::path canonical = profile_json_path(etc_root, profile_id);
    TrustedFilePolicy policy{
        .allow_current_user_owner = fs::absolute(etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
    try {
        return normalize_profile(Json::parse(read_trusted_config_file(canonical, policy)));
    } catch (const Json::exception& exc) {
        throw ValidationError("cannot read JSON profile " + canonical.string() + ": " + exc.what());
    }
}

Profile load_profile_by_id(const fs::path& etc_root, const std::string& profile_id) {
    return profile_from_json(load_profile_json_by_id(etc_root, profile_id));
}

} // namespace btrfsbackup
