// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/ManagerChangeMonitor.hpp>

#include <libudev.h>
#include <sys/inotify.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

#include <platform/linux/OwnedFileDescriptor.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t root_mask = IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF |
    IN_MOVE_SELF;
constexpr std::uint32_t file_mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF |
    IN_MOVE_SELF;

enum class WatchKind {
    Bootstrap,
    Profiles,
    StatusRoot,
    StatusProfile,
    HistoryRoot,
    HistoryProfile,
};

struct Watch {
    WatchKind kind;
    std::string profile_id;
};

std::runtime_error system_error(const char* operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

bool json_file(const std::string& name) {
    return name.size() > 5 && name.ends_with(".json");
}

} // namespace

namespace btrfsbackup::daemon {

class ManagerChangeMonitor::Impl final {
  public:
    Impl(const ManagerPaths& paths, Callback callback)
        : callback_(std::move(callback)),
          profiles_root_(paths.public_profile_root),
          status_root_(paths.status_root),
          history_root_(paths.history_root) {
        filesystem_fd_.reset(inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
        if (!filesystem_fd_.valid())
            throw system_error("cannot initialize manager filesystem notifications");
        refresh_watches();

        udev_.reset(udev_new());
        if (!udev_)
            throw std::runtime_error("cannot initialize udev manager notifications");
        device_monitor_.reset(udev_monitor_new_from_netlink(udev_.get(), "udev"));
        if (!device_monitor_)
            throw std::runtime_error("cannot initialize udev device monitor");
        if (udev_monitor_filter_add_match_subsystem_devtype(
                device_monitor_.get(),
                "block",
                nullptr
            ) < 0 ||
            udev_monitor_enable_receiving(device_monitor_.get()) < 0) {
            throw std::runtime_error("cannot enable udev device notifications");
        }
        device_fd_ = udev_monitor_get_fd(device_monitor_.get());

        mount_fd_.reset(open(paths.mountinfo_path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
        if (!mount_fd_.valid())
            throw system_error("cannot open mountinfo notifications");
        drain_mountinfo();
    }

    int filesystem_fd() const noexcept {
        return filesystem_fd_.get();
    }

    int device_fd() const noexcept {
        return device_fd_;
    }

    int mount_fd() const noexcept {
        return mount_fd_.get();
    }

    void process_filesystem_events() {
        alignas(inotify_event) std::array<char, 64 * 1024> buffer{};
        for (;;) {
            const ssize_t size = read(filesystem_fd_.get(), buffer.data(), buffer.size());
            if (size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                if (errno == EINTR)
                    continue;
                throw system_error("cannot read manager filesystem notifications");
            }
            if (size == 0)
                return;
            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(size)) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                process_event(*event);
                offset += sizeof(inotify_event) + event->len;
            }
        }
    }

    void process_device_events() {
        while (udev_device* raw_device = udev_monitor_receive_device(device_monitor_.get())) {
            std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
                raw_device,
                udev_device_unref
            );
            callback_({ManagerChangeKind::Device, {}});
        }
    }

    void process_mount_events() {
        drain_mountinfo();
        callback_({ManagerChangeKind::Device, {}});
    }

  private:
    void add_watch(
        const fs::path& path,
        WatchKind kind,
        std::string profile_id,
        std::uint32_t mask
    ) {
        const int descriptor = inotify_add_watch(filesystem_fd_.get(), path.c_str(), mask);
        if (descriptor < 0)
            throw system_error("cannot add manager filesystem watch");
        watches_.insert_or_assign(descriptor, Watch{kind, std::move(profile_id)});
    }

    void add_profile_watches(const fs::path& root, WatchKind kind) {
        std::error_code error;
        for (fs::directory_iterator iterator(root, error), end; !error && iterator != end;
             iterator.increment(error)) {
            if (iterator->is_directory(error) && !error) {
                add_watch(iterator->path(), kind, iterator->path().filename(), file_mask);
            }
        }
        if (error)
            throw std::runtime_error("cannot enumerate manager watch directory: " + error.message());
    }

    void add_bootstrap_watch(const fs::path& desired_root) {
        fs::path candidate = desired_root;
        std::error_code error;
        while (!candidate.empty() && !fs::is_directory(candidate, error)) {
            error.clear();
            const fs::path parent = candidate.parent_path();
            if (parent == candidate)
                break;
            candidate = parent;
        }
        if (candidate.empty() || !fs::is_directory(candidate, error) || error)
            throw std::runtime_error("cannot find a parent for manager filesystem watch");
        add_watch(candidate, WatchKind::Bootstrap, {}, root_mask);
    }

    void refresh_watches() {
        std::error_code error;
        if (fs::is_directory(profiles_root_, error) && !error)
            add_watch(profiles_root_, WatchKind::Profiles, {}, root_mask | file_mask);
        else
            add_bootstrap_watch(profiles_root_);

        error.clear();
        if (fs::is_directory(status_root_, error) && !error) {
            add_watch(status_root_, WatchKind::StatusRoot, {}, root_mask);
            add_profile_watches(status_root_, WatchKind::StatusProfile);
        } else {
            add_bootstrap_watch(status_root_);
        }

        error.clear();
        if (fs::is_directory(history_root_, error) && !error) {
            add_watch(history_root_, WatchKind::HistoryRoot, {}, root_mask);
            add_profile_watches(history_root_, WatchKind::HistoryProfile);
        } else {
            add_bootstrap_watch(history_root_);
        }
    }

    void process_event(const inotify_event& event) {
        if ((event.mask & IN_Q_OVERFLOW) != 0) {
            callback_({ManagerChangeKind::Profiles, {}});
            callback_({ManagerChangeKind::Status, {}});
            callback_({ManagerChangeKind::History, {}});
            callback_({ManagerChangeKind::Device, {}});
            return;
        }
        const auto found = watches_.find(event.wd);
        if (found == watches_.end())
            return;
        const Watch watch = found->second;
        const std::string name = event.len == 0 ? std::string{} : std::string(event.name);
        if ((event.mask & IN_IGNORED) != 0)
            watches_.erase(found);

        switch (watch.kind) {
        case WatchKind::Bootstrap:
            refresh_watches();
            callback_({ManagerChangeKind::Profiles, {}});
            callback_({ManagerChangeKind::Status, {}});
            callback_({ManagerChangeKind::History, {}});
            callback_({ManagerChangeKind::Device, {}});
            break;
        case WatchKind::Profiles:
            if (json_file(name))
                callback_({ManagerChangeKind::Profiles, {}});
            break;
        case WatchKind::StatusRoot:
            if (name.empty() || (event.mask & IN_ISDIR) == 0)
                break;
            process_profile_directory_event(event, name, WatchKind::StatusProfile);
            callback_({ManagerChangeKind::Status, name});
            callback_({ManagerChangeKind::Device, name});
            break;
        case WatchKind::HistoryRoot:
            if (name.empty() || (event.mask & IN_ISDIR) == 0)
                break;
            process_profile_directory_event(event, name, WatchKind::HistoryProfile);
            callback_({ManagerChangeKind::History, name});
            break;
        case WatchKind::StatusProfile:
            if (name == "current.json") {
                callback_({ManagerChangeKind::Status, watch.profile_id});
                callback_({ManagerChangeKind::Device, watch.profile_id});
            }
            break;
        case WatchKind::HistoryProfile:
            if (json_file(name))
                callback_({ManagerChangeKind::History, watch.profile_id});
            break;
        }
        if ((event.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) != 0)
            refresh_watches();
    }

    void process_profile_directory_event(
        const inotify_event& event,
        const std::string& name,
        WatchKind child_kind
    ) {
        if (name.empty() || (event.mask & IN_ISDIR) == 0 ||
            (event.mask & (IN_CREATE | IN_MOVED_TO)) == 0) {
            return;
        }
        const fs::path root = child_kind == WatchKind::StatusProfile ? status_root_ : history_root_;
        add_watch(root / name, child_kind, name, file_mask);
    }

    void drain_mountinfo() {
        if (lseek(mount_fd_.get(), 0, SEEK_SET) < 0)
            throw system_error("cannot rewind mountinfo notifications");
        std::array<char, 16 * 1024> buffer{};
        for (;;) {
            const ssize_t size = read(mount_fd_.get(), buffer.data(), buffer.size());
            if (size > 0)
                continue;
            if (size == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            if (errno != EINTR)
                throw system_error("cannot read mountinfo notifications");
        }
    }

    struct UdevDeleter {
        void operator()(udev* value) const noexcept {
            udev_unref(value);
        }
    };
    struct UdevMonitorDeleter {
        void operator()(udev_monitor* value) const noexcept {
            udev_monitor_unref(value);
        }
    };

    Callback callback_;
    fs::path profiles_root_;
    fs::path status_root_;
    fs::path history_root_;
    btrfsbackup::platform::linux::OwnedFileDescriptor filesystem_fd_;
    int device_fd_ = -1;
    btrfsbackup::platform::linux::OwnedFileDescriptor mount_fd_;
    std::unique_ptr<udev, UdevDeleter> udev_;
    std::unique_ptr<udev_monitor, UdevMonitorDeleter> device_monitor_;
    std::unordered_map<int, Watch> watches_;
};

ManagerChangeMonitor::ManagerChangeMonitor(const ManagerPaths& paths, Callback callback)
    : impl_(std::make_unique<Impl>(paths, std::move(callback))) {
}

ManagerChangeMonitor::~ManagerChangeMonitor() = default;

int ManagerChangeMonitor::filesystem_fd() const noexcept {
    return impl_->filesystem_fd();
}

int ManagerChangeMonitor::device_fd() const noexcept {
    return impl_->device_fd();
}

int ManagerChangeMonitor::mount_fd() const noexcept {
    return impl_->mount_fd();
}

void ManagerChangeMonitor::process_filesystem_events() {
    impl_->process_filesystem_events();
}

void ManagerChangeMonitor::process_device_events() {
    impl_->process_device_events();
}

void ManagerChangeMonitor::process_mount_events() {
    impl_->process_mount_events();
}

} // namespace btrfsbackup::daemon
