// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CancellationRequestDispatcher.hpp"

#include "ManagerApi.hpp"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <core/ManagerProtocol.hpp>

#include <utility>

namespace btrfsbackup::kde::monitor {

CancellationRequestDispatcher::CancellationRequestDispatcher(QDBusConnection bus, QObject* parent)
    : QObject(parent), bus_(std::move(bus)) {
}

void CancellationRequestDispatcher::request(const QString& profile_id, const QString& run_id) {
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(
            bus_,
            QLatin1String(btrfsbackup::manager_protocol::method::cancel_backup),
            {profile_id, run_id}
        ),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, profile_id, run_id](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            Q_EMIT rejected(profile_id, run_id, reply.error().message());
            return;
        }

        const auto result = btrfsbackup::kde::parse_operation_result(reply.value());
        if (!result.has_value() || !result->accepted ||
            result->operation != QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup) ||
            result->profile_id != profile_id || result->run_id != run_id) {
            Q_EMIT rejected(profile_id, run_id, QStringLiteral("invalid cancellation response"));
            return;
        }
        Q_EMIT accepted(profile_id, run_id);
    });
}

} // namespace btrfsbackup::kde::monitor
