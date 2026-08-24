#include <btrfsbackup/system/filesystem.hpp>

#include <algorithm>

#include <btrfsbackup/model/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

bool PosixFileSystem::exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

bool PosixFileSystem::is_directory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

void PosixFileSystem::create_directories(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) throw ValidationError("could not create directory " + path.string() + ": " + ec.message());
}

void PosixFileSystem::remove_file(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) throw ValidationError("could not remove file " + path.string() + ": " + ec.message());
}

void PosixFileSystem::remove_directory(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) throw ValidationError("could not remove directory " + path.string() + ": " + ec.message());
}

void PosixFileSystem::remove_tree(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) throw ValidationError("could not remove path tree " + path.string() + ": " + ec.message());
}

void PosixFileSystem::rename_path(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    fs::rename(source, target, ec);
    if (ec) throw ValidationError("could not rename " + source.string() + " to " + target.string() + ": " + ec.message());
}

std::vector<fs::path> PosixFileSystem::list_directory(const fs::path& path) {
    std::vector<fs::path> entries;
    std::error_code ec;
    if (!fs::is_directory(path, ec) || ec) return entries;
    for (const fs::directory_entry& entry : fs::directory_iterator(path, ec)) {
        if (ec) throw ValidationError("could not list directory " + path.string() + ": " + ec.message());
        entries.push_back(entry.path());
    }
    if (ec) throw ValidationError("could not list directory " + path.string() + ": " + ec.message());
    std::sort(entries.begin(), entries.end());
    return entries;
}

} // namespace btrfsbackup
