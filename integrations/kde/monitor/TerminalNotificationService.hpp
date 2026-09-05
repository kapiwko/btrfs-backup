// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "NotificationDeduplicator.hpp"

#include <QString>

#include <functional>

namespace btrfsbackup::kde::monitor {

struct TerminalNotificationMessage {
    QString event_id;
    QString title;
    QString text;
    QString error_code;
    QString profile_id;
};

class TerminalNotificationService final {
  public:
    using Publisher = std::function<void(const TerminalNotificationMessage&)>;

    explicit TerminalNotificationService(QString state_path = {}, Publisher publisher = {});

    void publish(
        const QString& profile_id,
        const QString& run_id,
        const QString& profile_name,
        const QString& operation_kind,
        const QString& terminal_state,
        const QString& error_code
    );

  private:
    static void publish_to_desktop(const TerminalNotificationMessage& message);

    NotificationDeduplicator deduplicator_;
    Publisher publisher_;
};

} // namespace btrfsbackup::kde::monitor
