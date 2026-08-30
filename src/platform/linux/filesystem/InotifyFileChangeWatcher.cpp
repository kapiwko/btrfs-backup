// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/InotifyFileChangeWatcher.hpp>

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <platform/linux/OwnedFileDescriptor.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t watch_mask = IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
    IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_TO;

std::runtime_error system_error(const char* operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

int poll_timeout(std::optional<std::chrono::milliseconds> timeout) {
    if (!timeout.has_value()) {
        return -1;
    }
    return timeout->count() > INT_MAX ? INT_MAX : static_cast<int>(timeout->count());
}

} // namespace

namespace btrfsbackup::platform::linux::filesystem {

class InotifyFileChangeWatcher::Impl final {
  public:
    explicit Impl(fs::path path)
        : target_path_(fs::absolute(path).lexically_normal()),
          desired_directory_(target_path_.parent_path()) {
        descriptor_.reset(inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
        if (!descriptor_.valid()) {
            throw system_error("cannot initialize file change notifications");
        }
        refresh_watch();
    }

    void wait_for_change(std::optional<std::chrono::milliseconds> resync_timeout) {
        pollfd interest{.fd = descriptor_.get(), .events = POLLIN, .revents = 0};
        int result;
        do {
            result = poll(&interest, 1, poll_timeout(resync_timeout));
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            throw system_error("cannot wait for file change notification");
        }
        if (result == 0) {
            return;
        }
        if ((interest.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            throw std::runtime_error("file change notification descriptor failed");
        }
        drain_events();
        refresh_watch();
    }

  private:
    void refresh_watch() {
        fs::path candidate = desired_directory_.empty() ? fs::path(".") : desired_directory_;
        std::error_code error;
        while (!fs::is_directory(candidate, error)) {
            error.clear();
            const fs::path parent = candidate.parent_path();
            if (parent.empty() || parent == candidate) {
                throw std::runtime_error(
                    "cannot find existing parent for file change notifications: " +
                    target_path_.string()
                );
            }
            candidate = parent;
        }

        if (watch_descriptor_ >= 0 && candidate == watched_directory_) {
            return;
        }

        if (watch_descriptor_ >= 0) {
            const int previous_watch = watch_descriptor_;
            watch_descriptor_ = -1;
            (void)inotify_rm_watch(descriptor_.get(), previous_watch);
            drain_events();
        }
        watch_descriptor_ = inotify_add_watch(descriptor_.get(), candidate.c_str(), watch_mask);
        if (watch_descriptor_ < 0) {
            throw system_error("cannot add file change notification watch");
        }
        watched_directory_ = std::move(candidate);
    }

    void drain_events() {
        alignas(inotify_event) std::array<char, 16 * 1024> buffer{};
        while (true) {
            const ssize_t size = read(descriptor_.get(), buffer.data(), buffer.size());
            if (size > 0) {
                std::size_t offset = 0;
                while (offset < static_cast<std::size_t>(size)) {
                    const auto* event = reinterpret_cast<const inotify_event*>(
                        buffer.data() + offset
                    );
                    if (event->wd == watch_descriptor_ && (event->mask & IN_IGNORED) != 0) {
                        watch_descriptor_ = -1;
                    }
                    offset += sizeof(inotify_event) + event->len;
                }
                continue;
            }
            if (size < 0 && errno == EINTR) {
                continue;
            }
            if (size == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw system_error("cannot read file change notifications");
        }
    }

    fs::path target_path_;
    fs::path desired_directory_;
    fs::path watched_directory_;
    OwnedFileDescriptor descriptor_;
    int watch_descriptor_ = -1;
};

InotifyFileChangeWatcher::InotifyFileChangeWatcher(fs::path path)
    : impl_(std::make_unique<Impl>(std::move(path))) {
}

InotifyFileChangeWatcher::~InotifyFileChangeWatcher() noexcept = default;

void InotifyFileChangeWatcher::wait_for_change(
    std::optional<std::chrono::milliseconds> resync_timeout
) {
    impl_->wait_for_change(resync_timeout);
}

} // namespace btrfsbackup::platform::linux::filesystem
