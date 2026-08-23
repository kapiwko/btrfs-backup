#include <btrfsbackup/json_io.hpp>

#include <fstream>

#include <btrfsbackup/errors.hpp>

namespace btrfsbackup {

Json load_json_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw ValidationError("cannot read JSON profile " + path.string());
    }
    try {
        Json data;
        stream >> data;
        return data;
    } catch (const std::exception& exc) {
        throw ValidationError("cannot read JSON profile " + path.string() + ": " + exc.what());
    }
}

std::string dump_json(const Json& data) {
    return data.dump(2) + "\n";
}

} // namespace btrfsbackup
