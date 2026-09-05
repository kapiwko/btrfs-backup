// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseFilesystemAccess.hpp>

#include <daemon/control/BrowseDirectoryPageCollector.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <acl/libacl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

[[noreturn]] void path_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::vector<std::string> validated_components(const fs::path& relative) {
    if (relative.is_absolute())
        throw std::invalid_argument("browse path must be relative");
    std::vector<std::string> result;
    for (const fs::path& component : relative) {
        const std::string value = component.string();
        if (value.empty() || value == ".")
            continue;
        if (value == ".." || value.find('/') != std::string::npos || value.find('\0') != std::string::npos)
            throw std::invalid_argument("browse path contains an unsafe component");
        result.push_back(value);
    }
    return result;
}

OwnedFileDescriptor open_beneath(int root, const fs::path& relative, int final_flags) {
    OwnedFileDescriptor current(openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!current.valid())
        path_error("cannot open browse root");
    const auto components = validated_components(relative);
    if (components.empty()) {
        OwnedFileDescriptor result(openat(current.get(), ".", final_flags | O_CLOEXEC | O_NOFOLLOW));
        if (!result.valid())
            path_error("cannot open browse entry");
        return result;
    }
    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        OwnedFileDescriptor next(openat(
            current.get(),
            components[index].c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        ));
        if (!next.valid())
            path_error("cannot traverse browse entry");
        current = std::move(next);
    }
    OwnedFileDescriptor result(openat(
        current.get(),
        components.back().c_str(),
        final_flags | O_CLOEXEC | O_NOFOLLOW
    ));
    if (!result.valid())
        path_error("cannot open browse entry");
    return result;
}

OwnedFileDescriptor session_root(const fs::path& root) {
    OwnedFileDescriptor descriptor(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        path_error("cannot open browse session view");
    return descriptor;
}

struct AclCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr)
            acl_free(value);
    }
};

using OwnedAcl = std::unique_ptr<std::remove_pointer_t<acl_t>, AclCloser>;
using OwnedAclQualifier = std::unique_ptr<void, AclCloser>;

bool contains_group(const BrowseAccessIdentity& identity, gid_t group) {
    return std::ranges::find(identity.groups, static_cast<std::uint32_t>(group)) != identity.groups.end();
}

int acl_permissions(acl_permset_t permissions) {
    const int read = acl_get_perm(permissions, ACL_READ);
    const int write = acl_get_perm(permissions, ACL_WRITE);
    const int execute = acl_get_perm(permissions, ACL_EXECUTE);
    if (read < 0 || write < 0 || execute < 0)
        path_error("cannot read stored POSIX ACL permissions");
    return (read == 1 ? 4 : 0) | (write == 1 ? 2 : 0) | (execute == 1 ? 1 : 0);
}

int mode_permissions(const struct stat& status, const BrowseAccessIdentity& identity) {
    if (status.st_uid == static_cast<uid_t>(identity.uid))
        return (status.st_mode >> 6) & 7;
    if (contains_group(identity, status.st_gid))
        return (status.st_mode >> 3) & 7;
    return status.st_mode & 7;
}

int effective_permissions(int descriptor, const struct stat& status, const BrowseAccessIdentity& identity) {
    OwnedAcl acl(acl_get_fd(descriptor));
    if (!acl && (errno == ENOTSUP || errno == EOPNOTSUPP || errno == ENOSYS))
        return mode_permissions(status, identity);
    if (!acl)
        path_error("cannot read stored POSIX ACL");

    int owner = -1;
    int named_user = -1;
    int matching_groups = 0;
    bool group_matched = false;
    int other = 0;
    int mask = 7;
    acl_entry_t entry{};
    int entry_id = ACL_FIRST_ENTRY;
    int entry_result = 0;
    while ((entry_result = acl_get_entry(acl.get(), entry_id, &entry)) == 1) {
        entry_id = ACL_NEXT_ENTRY;
        acl_tag_t tag{};
        acl_permset_t permissions{};
        if (acl_get_tag_type(entry, &tag) != 0 || acl_get_permset(entry, &permissions) != 0)
            path_error("cannot read stored POSIX ACL entry");
        const int value = acl_permissions(permissions);
        if (tag == ACL_USER_OBJ) {
            owner = value;
        } else if (tag == ACL_USER) {
            OwnedAclQualifier qualifier(acl_get_qualifier(entry));
            if (!qualifier)
                path_error("cannot read stored POSIX ACL user");
            if (*static_cast<uid_t*>(qualifier.get()) == static_cast<uid_t>(identity.uid))
                named_user = value;
        } else if (tag == ACL_GROUP_OBJ) {
            if (contains_group(identity, status.st_gid)) {
                matching_groups |= value;
                group_matched = true;
            }
        } else if (tag == ACL_GROUP) {
            OwnedAclQualifier qualifier(acl_get_qualifier(entry));
            if (!qualifier)
                path_error("cannot read stored POSIX ACL group");
            if (contains_group(identity, *static_cast<gid_t*>(qualifier.get()))) {
                matching_groups |= value;
                group_matched = true;
            }
        } else if (tag == ACL_MASK) {
            mask = value;
        } else if (tag == ACL_OTHER) {
            other = value;
        }
    }
    if (entry_result < 0)
        path_error("cannot enumerate stored POSIX ACL");
    if (status.st_uid == static_cast<uid_t>(identity.uid))
        return owner >= 0 ? owner : mode_permissions(status, identity);
    if (named_user >= 0)
        return named_user & mask;
    if (group_matched)
        return matching_groups & mask;
    return other;
}

void require_access(int descriptor, const BrowseAccessIdentity* identity, int required, const char* operation) {
    if (identity == nullptr || identity->uid == 0)
        return;
    struct stat status{};
    if (fstat(descriptor, &status) != 0)
        path_error("cannot inspect browse permissions");
    if ((effective_permissions(descriptor, status, *identity) & required) != required)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, operation);
}

OwnedFileDescriptor open_authorized_directory(
    int root,
    const fs::path& relative,
    const BrowseAccessIdentity* identity,
    int final_permissions
) {
    OwnedFileDescriptor current(openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!current.valid())
        path_error("cannot open browse root");
    require_access(current.get(), identity, 1, "stored directory permissions deny traversal");
    for (const std::string& component : validated_components(relative)) {
        OwnedFileDescriptor next(openat(
            current.get(),
            component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        ));
        if (!next.valid())
            path_error("cannot traverse browse entry");
        current = std::move(next);
        require_access(current.get(), identity, 1, "stored directory permissions deny traversal");
    }
    require_access(current.get(), identity, final_permissions, "stored directory permissions deny listing");
    return current;
}

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept {
        if (directory != nullptr)
            closedir(directory);
    }
};

std::unique_ptr<DIR, DirectoryCloser> directory_stream(int descriptor) {
    const int duplicate = dup(descriptor);
    if (duplicate < 0)
        path_error("cannot duplicate browse directory");
    std::unique_ptr<DIR, DirectoryCloser> stream(fdopendir(duplicate));
    if (!stream) {
        ::close(duplicate);
        path_error("cannot open browse directory stream");
    }
    return stream;
}

BrowseEntryInfo entry_info(int parent, const std::string& name) {
    struct stat status{};
    if (fstatat(parent, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
        path_error("cannot inspect browse entry");
    if (S_ISLNK(status.st_mode) || (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        name,
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

} // namespace

fs::path BrowseFilesystemAccess::normalize_relative_path(const fs::path& relative_path) {
    fs::path result;
    for (const std::string& component : validated_components(relative_path))
        result /= component;
    return result.empty() ? fs::path{"."} : result;
}

std::vector<BrowseEntryInfo> BrowseFilesystemAccess::list_directory(
    const fs::path& root,
    const fs::path& relative_path,
    std::size_t maximum_entries,
    const BrowseAccessIdentity* identity
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor directory = open_authorized_directory(root_descriptor.get(), relative_path, identity, 5);
    auto stream = directory_stream(directory.get());
    std::vector<BrowseEntryInfo> result;
    errno = 0;
    while (dirent* item = readdir(stream.get())) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name == ".incoming")
            continue;
        try {
            BrowseEntryInfo info = entry_info(directory.get(), name);
            if (result.size() >= maximum_entries)
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse directory exceeds the safe entry limit");
            result.push_back(std::move(info));
        } catch (const std::invalid_argument&) {
            continue;
        }
        errno = 0;
    }
    if (errno != 0)
        path_error("cannot read browse directory");
    return result;
}

BrowseDirectoryPage BrowseFilesystemAccess::list_directory_page(
    const fs::path& root,
    const fs::path& relative_path,
    const std::string& after_name,
    std::size_t maximum_entries,
    const BrowseAccessIdentity* identity
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor directory = open_authorized_directory(root_descriptor.get(), relative_path, identity, 5);
    auto stream = directory_stream(directory.get());
    BrowseDirectoryPageCollector entries(maximum_entries);
    errno = 0;
    while (dirent* item = readdir(stream.get())) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name == ".incoming" || name <= after_name)
            continue;
        try {
            entries.add(entry_info(directory.get(), name));
        } catch (const std::invalid_argument&) {
            continue;
        }
        errno = 0;
    }
    if (errno != 0)
        path_error("cannot read browse directory");
    return entries.finish();
}

BrowseEntryInfo BrowseFilesystemAccess::inspect_entry(
    const fs::path& root,
    const fs::path& relative_path,
    const BrowseAccessIdentity* identity
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    const fs::path parent = relative_path.parent_path();
    (void)open_authorized_directory(root_descriptor.get(), parent.empty() ? fs::path{"."} : parent, identity, 1);
    OwnedFileDescriptor entry = open_beneath(root_descriptor.get(), relative_path, O_PATH);
    struct stat status{};
    if (fstat(entry.get(), &status) != 0)
        path_error("cannot inspect browse entry");
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        relative_path.filename().string(),
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

OwnedFileDescriptor BrowseFilesystemAccess::open_file(
    const fs::path& root,
    const fs::path& relative_path,
    const BrowseAccessIdentity* identity
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    const fs::path parent = relative_path.parent_path();
    (void)open_authorized_directory(root_descriptor.get(), parent.empty() ? fs::path{"."} : parent, identity, 1);
    OwnedFileDescriptor result = open_beneath(root_descriptor.get(), relative_path, O_RDONLY | O_NONBLOCK);
    struct stat status{};
    if (fstat(result.get(), &status) != 0)
        path_error("cannot inspect browse file");
    if (!S_ISREG(status.st_mode))
        throw std::invalid_argument("browse entry is not a regular file");
    require_access(result.get(), identity, 4, "stored file permissions deny reading");
    return result;
}

OwnedFileDescriptor BrowseFilesystemAccess::open_root(const fs::path& root) const {
    OwnedFileDescriptor result(::open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!result.valid())
        path_error("cannot open browse session root");
    return result;
}

} // namespace btrfsbackup::daemon::control
