#include <btrfsbackup/system/device_info.hpp>

#include <system_error>

namespace fs = std::filesystem;

namespace btrfsbackup {

fs::path mapper_path(const std::string& mapper_name, const fs::path& mapper_root) {
    return mapper_root / mapper_name;
}

fs::path canonical_device(const fs::path& path) {
    std::error_code ec;
    fs::path result = fs::canonical(path, ec);
    if (ec) {
        return {};
    }
    return result;
}

std::string strip_subvolume_suffix(const std::string& source) {
    std::size_t bracket = source.find('[');
    if (bracket == std::string::npos) {
        return source;
    }
    return source.substr(0, bracket);
}

} // namespace btrfsbackup
