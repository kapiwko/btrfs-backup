// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TargetStatusParser.hpp"

#include <limits>

#include <core/RuntimeTime.hpp>
#include <state/document/TargetStatusDocumentCodec.hpp>

namespace btrfsbackup::kde {

std::optional<TargetStatus> parse_target_status(const QString& payload) {
    const auto decoded = btrfsbackup::state::document::TargetStatusDocumentCodec{}.try_parse(payload.toStdString());
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    const auto& status = *decoded;
    TargetStatus result{
        .profile_id = QString::fromStdString(status.profile_id),
        .target_name = QString::fromStdString(status.target_name),
        .state = QString::fromStdString(status.state),
        .connected = status.connected,
        .unlocked = status.unlocked,
        .mounted = status.mounted,
        .safe_to_remove = status.safe_to_remove,
        .storage = std::nullopt,
    };
    if (status.storage.has_value()) {
        constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
        if (status.storage->capacity_bytes > maximum || status.storage->used_bytes > maximum ||
            status.storage->available_bytes > maximum) {
            return result;
        }
        result.storage = TargetStorageStatus{
            .capacity_bytes = static_cast<qint64>(status.storage->capacity_bytes),
            .used_bytes = static_cast<qint64>(status.storage->used_bytes),
            .available_bytes = static_cast<qint64>(status.storage->available_bytes),
            .usage_percent = status.storage->usage_percent,
            .measured_at = QString::fromStdString(format_utc_iso_timestamp(status.storage->measured_at)),
            .live = status.storage->live,
            .space_state = QString::fromStdString(
                btrfsbackup::state::document::target_space_state_name(status.storage->space_state)
            ),
        };
    }
    return result;
}

} // namespace btrfsbackup::kde
