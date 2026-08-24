#include <btrfsbackup/system/safe_directory_root.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <linux/btrfs.h>
#include <linux/btrfs_tree.h>
#include <linux/openat2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <btrfsbackup/model/errors.hpp>
#include <btrfsbackup/model/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

[[noreturn]] void throw_path_error(const std::string& operation, const fs::path& path, int error) {
    throw ValidationError(operation + " " + path.string() + ": " + std::strerror(error));
}

int openat2_no_symlinks(int directory_fd, const fs::path& path, int flags) {
    struct open_how how {};
    how.flags = static_cast<unsigned long long>(flags | O_CLOEXEC);
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    return static_cast<int>(syscall(SYS_openat2, directory_fd, path.c_str(), &how, sizeof(how)));
}

SafeDirectoryHandle duplicate_fd(int fd, const fs::path& path) {
    int duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0) {
        throw_path_error("cannot duplicate safe directory descriptor for", path, errno);
    }
    return SafeDirectoryHandle(duplicate);
}

std::vector<std::string> directory_names(int fd, const fs::path& display_path) {
    int duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0) {
        throw_path_error("cannot duplicate directory descriptor for", display_path, errno);
    }
    DIR* directory = fdopendir(duplicate);
    if (directory == nullptr) {
        close(duplicate);
        throw_path_error("cannot list directory", display_path, errno);
    }

    std::vector<std::string> result;
    errno = 0;
    while (dirent* entry = readdir(directory)) {
        std::string name = entry->d_name;
        if (name != "." && name != "..") {
            result.push_back(std::move(name));
        }
        errno = 0;
    }
    if (errno != 0) {
        int read_error = errno;
        closedir(directory);
        throw_path_error("cannot list directory", display_path, read_error);
    }
    closedir(directory);
    std::sort(result.begin(), result.end());
    return result;
}

void destroy_subvolume_at(int parent_fd, const std::string& name, const fs::path& display_path) {
    if (name.size() >= BTRFS_PATH_NAME_MAX) {
        throw ValidationError("subvolume name is too long: " + display_path.string());
    }
    struct btrfs_ioctl_vol_args arguments {};
    std::memcpy(arguments.name, name.c_str(), name.size() + 1);
    if (ioctl(parent_fd, BTRFS_IOC_SNAP_DESTROY, &arguments) != 0) {
        throw_path_error("cannot delete Btrfs subvolume", display_path, errno);
    }
}

void remove_entry(int parent_fd, const std::string& name, const fs::path& display_path) {
    struct stat status {};
    if (fstatat(parent_fd, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return;
        }
        throw_path_error("cannot inspect path", display_path, errno);
    }
    if (S_ISLNK(status.st_mode)) {
        throw ValidationError("symbolic link is forbidden below safe directory root: " + display_path.string());
    }
    if (!S_ISDIR(status.st_mode)) {
        if (unlinkat(parent_fd, name.c_str(), 0) != 0) {
            throw_path_error("cannot remove file", display_path, errno);
        }
        return;
    }

    int child_fd = openat2_no_symlinks(parent_fd, name, O_RDONLY | O_DIRECTORY);
    if (child_fd < 0) {
        throw_path_error("cannot open directory without symlinks", display_path, errno);
    }
    SafeDirectoryHandle child(child_fd);
    if (status.st_ino == BTRFS_FIRST_FREE_OBJECTID) {
        destroy_subvolume_at(parent_fd, name, display_path);
        return;
    }
    for (const std::string& child_name : directory_names(child.fd(), display_path)) {
        remove_entry(child.fd(), child_name, display_path / child_name);
    }
    if (unlinkat(parent_fd, name.c_str(), AT_REMOVEDIR) != 0) {
        throw_path_error("cannot remove directory", display_path, errno);
    }
}

} // namespace

SafeDirectoryHandle::SafeDirectoryHandle(int fd) : fd_(fd) {
    if (fd_ >= 0 && fd_ < 3) {
        int duplicate = fcntl(fd_, F_DUPFD_CLOEXEC, 3);
        int duplicate_error = errno;
        close(fd_);
        fd_ = duplicate;
        if (fd_ < 0) {
            throw ValidationError(std::string("cannot move safe descriptor above standard streams: ") + std::strerror(duplicate_error));
        }
    }
}

SafeDirectoryHandle::SafeDirectoryHandle(SafeDirectoryHandle&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

SafeDirectoryHandle& SafeDirectoryHandle::operator=(SafeDirectoryHandle&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

SafeDirectoryHandle::~SafeDirectoryHandle() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

int SafeDirectoryHandle::fd() const noexcept {
    return fd_;
}

fs::path SafeDirectoryHandle::proc_path() const {
    if (fd_ < 0) {
        throw ValidationError("safe directory descriptor is not open");
    }
    return fs::path("/proc/self/fd") / std::to_string(fd_);
}

SafeDirectoryRoot::SafeDirectoryRoot(const fs::path& root)
    : root_path_(normalized_path(root)) {
    if (!root_path_.is_absolute()) {
        throw ValidationError("safe directory root must be absolute: " + root.string());
    }
    int slash_fd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (slash_fd < 0) {
        throw_path_error("cannot open filesystem root for", root_path_, errno);
    }
    SafeDirectoryHandle slash(slash_fd);
    fs::path relative = root_path_.lexically_relative("/");
    if (relative.empty()) {
        relative = ".";
    }
    int root_fd = openat2_no_symlinks(slash.fd(), relative, O_PATH | O_DIRECTORY);
    if (root_fd < 0) {
        throw_path_error("cannot open safe directory root", root_path_, errno);
    }
    root_ = SafeDirectoryHandle(root_fd);
}

const fs::path& SafeDirectoryRoot::path() const noexcept {
    return root_path_;
}

fs::path SafeDirectoryRoot::relative_path(const fs::path& path) const {
    fs::path normalized = normalized_path(path);
    if (!normalized.is_absolute() || !path_is_within(normalized, root_path_)) {
        throw ValidationError("path escapes safe directory root " + root_path_.string() + ": " + path.string());
    }
    fs::path relative = normalized.lexically_relative(root_path_);
    return relative.empty() ? fs::path(".") : relative;
}

SafeDirectoryHandle SafeDirectoryRoot::open_relative(const fs::path& relative, int flags) const {
    int fd = openat2_no_symlinks(root_.fd(), relative, flags);
    if (fd < 0) {
        throw_path_error("cannot open path below safe directory root", root_path_ / relative, errno);
    }
    return SafeDirectoryHandle(fd);
}

SafeDirectoryHandle SafeDirectoryRoot::open_directory(const fs::path& path) const {
    return open_relative(relative_path(path), O_RDONLY | O_DIRECTORY);
}

SafeDirectoryHandle SafeDirectoryRoot::open_path(const fs::path& path) const {
    return open_relative(relative_path(path), O_PATH);
}

void SafeDirectoryRoot::ensure_directory(const fs::path& path, unsigned int mode) const {
    fs::path relative = relative_path(path);
    SafeDirectoryHandle current = duplicate_fd(root_.fd(), root_path_);
    fs::path display = root_path_;
    for (const fs::path& component : relative) {
        if (component == ".") {
            continue;
        }
        display /= component;
        int next_fd = openat2_no_symlinks(current.fd(), component, O_PATH | O_DIRECTORY);
        if (next_fd < 0 && errno == ENOENT) {
            if (mkdirat(current.fd(), component.c_str(), static_cast<mode_t>(mode)) != 0 && errno != EEXIST) {
                throw_path_error("cannot create directory", display, errno);
            }
            next_fd = openat2_no_symlinks(current.fd(), component, O_PATH | O_DIRECTORY);
        }
        if (next_fd < 0) {
            throw_path_error("cannot open directory without symlinks", display, errno);
        }
        current = SafeDirectoryHandle(next_fd);
    }
}

bool SafeDirectoryRoot::exists(const fs::path& path) const {
    fs::path relative = relative_path(path);
    int fd = openat2_no_symlinks(root_.fd(), relative, O_PATH);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    if (errno == ENOENT) {
        return false;
    }
    throw_path_error("cannot inspect path below safe directory root", path, errno);
}

void SafeDirectoryRoot::remove_contents(const fs::path& directory) const {
    if (!exists(directory)) {
        return;
    }
    SafeDirectoryHandle handle = open_directory(directory);
    for (const std::string& name : directory_names(handle.fd(), directory)) {
        remove_entry(handle.fd(), name, directory / name);
    }
}

void SafeDirectoryRoot::remove_tree(const fs::path& path) const {
    fs::path relative = relative_path(path);
    if (relative == ".") {
        throw ValidationError("refusing to remove safe directory root: " + root_path_.string());
    }
    fs::path parent_relative = relative.parent_path();
    SafeDirectoryHandle parent = open_relative(parent_relative.empty() ? fs::path(".") : parent_relative, O_RDONLY | O_DIRECTORY);
    remove_entry(parent.fd(), relative.filename().string(), path);
}

void SafeDirectoryRoot::delete_subvolume(const fs::path& path) const {
    fs::path relative = relative_path(path);
    if (relative == ".") {
        throw ValidationError("refusing to delete safe directory root as a subvolume: " + root_path_.string());
    }
    fs::path parent_relative = relative.parent_path();
    SafeDirectoryHandle parent = open_relative(parent_relative.empty() ? fs::path(".") : parent_relative, O_RDONLY | O_DIRECTORY);
    const std::string name = relative.filename().string();
    struct stat status {};
    if (fstatat(parent.fd(), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        throw_path_error("cannot inspect Btrfs subvolume", path, errno);
    }
    if (S_ISLNK(status.st_mode) || !S_ISDIR(status.st_mode) || status.st_ino != BTRFS_FIRST_FREE_OBJECTID) {
        throw ValidationError("refusing to delete a path that is not a directly opened Btrfs subvolume: " + path.string());
    }
    SafeDirectoryHandle child = open_relative(relative, O_PATH | O_DIRECTORY);
    (void)child;
    destroy_subvolume_at(parent.fd(), name, path);
}

} // namespace btrfsbackup
