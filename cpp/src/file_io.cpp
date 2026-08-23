#include <btrfsbackup/file_io.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <vector>

#include <btrfsbackup/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

void atomic_write(const fs::path& path, const std::string& data, mode_t mode) {
    fs::create_directories(path.parent_path());
    std::string pattern = (path.parent_path() / ("." + path.filename().string() + ".XXXXXX")).string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    int fd = mkstemp(writable.data());
    if (fd < 0) {
        throw ValidationError("cannot create temporary file for " + path.string());
    }
    fs::path temporary(writable.data());
    try {
        fchmod(fd, mode);
        const char* ptr = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            ssize_t written = write(fd, ptr, remaining);
            if (written < 0) {
                throw ValidationError("cannot write " + temporary.string());
            }
            ptr += written;
            remaining -= static_cast<std::size_t>(written);
        }
        fsync(fd);
        close(fd);
        fd = -1;
        fs::rename(temporary, path);
        chmod(path.c_str(), mode);
    } catch (...) {
        if (fd >= 0) {
            close(fd);
        }
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}

} // namespace btrfsbackup
