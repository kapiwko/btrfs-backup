// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/StoredPermissionEvaluator.hpp>

#include <daemon/dbus/ManagerErrors.hpp>

#include <acl/libacl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <ranges>
#include <system_error>

namespace btrfsbackup::daemon::control {
namespace {

[[noreturn]] void permission_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
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
        permission_error("cannot read stored POSIX ACL permissions");
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
        permission_error("cannot read stored POSIX ACL");

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
            permission_error("cannot read stored POSIX ACL entry");
        const int value = acl_permissions(permissions);
        if (tag == ACL_USER_OBJ) {
            owner = value;
        } else if (tag == ACL_USER) {
            OwnedAclQualifier qualifier(acl_get_qualifier(entry));
            if (!qualifier)
                permission_error("cannot read stored POSIX ACL user");
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
                permission_error("cannot read stored POSIX ACL group");
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
        permission_error("cannot enumerate stored POSIX ACL");
    if (status.st_uid == static_cast<uid_t>(identity.uid))
        return owner >= 0 ? owner : mode_permissions(status, identity);
    if (named_user >= 0)
        return named_user & mask;
    if (group_matched)
        return matching_groups & mask;
    return other;
}

} // namespace

void StoredPermissionEvaluator::require_access(
    int descriptor,
    const BrowseAccessIdentity* identity,
    int required,
    const char* operation
) {
    if (identity == nullptr || identity->uid == 0)
        return;
    struct stat status{};
    if (fstat(descriptor, &status) != 0)
        permission_error("cannot inspect browse permissions");
    if ((effective_permissions(descriptor, status, *identity) & required) != required)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, operation);
}

} // namespace btrfsbackup::daemon::control
