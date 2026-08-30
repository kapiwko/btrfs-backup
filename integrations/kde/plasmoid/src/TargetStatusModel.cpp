// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TargetStatusModel.hpp"

#include "ByteFormatting.hpp"
#include "TargetStatusParser.hpp"

TargetStatusModel::TargetStatusModel(QObject* parent)
    : QObject(parent) {
}

QString TargetStatusModel::name() const {
    return name_;
}

QString TargetStatusModel::state() const {
    return state_;
}

bool TargetStatusModel::connected() const {
    return connected_;
}

bool TargetStatusModel::unlocked() const {
    return unlocked_;
}

bool TargetStatusModel::mounted() const {
    return mounted_;
}

bool TargetStatusModel::safeToRemove() const {
    return safe_to_remove_;
}

bool TargetStatusModel::storageSupported() const {
    return storage_supported_;
}

bool TargetStatusModel::storageKnown() const {
    return storage_known_;
}

qint64 TargetStatusModel::capacityBytes() const {
    return capacity_bytes_;
}

qint64 TargetStatusModel::usedBytes() const {
    return used_bytes_;
}

qint64 TargetStatusModel::availableBytes() const {
    return available_bytes_;
}

QString TargetStatusModel::capacityText() const {
    return btrfsbackup::kde::format_byte_size(capacity_bytes_);
}

QString TargetStatusModel::usedText() const {
    return btrfsbackup::kde::format_byte_size(used_bytes_);
}

QString TargetStatusModel::availableText() const {
    return btrfsbackup::kde::format_byte_size(available_bytes_);
}

int TargetStatusModel::usagePercent() const {
    return usage_percent_;
}

bool TargetStatusModel::storageLive() const {
    return storage_live_;
}

QString TargetStatusModel::storageMeasuredAt() const {
    return storage_measured_at_;
}

bool TargetStatusModel::spaceBelowMinimum() const {
    return space_below_minimum_;
}

void TargetStatusModel::setStorageSupported(bool supported) {
    if (storage_supported_ == supported) {
        return;
    }
    storage_supported_ = supported;
    if (!storage_supported_) {
        resetStorage();
    }
    emit changed();
}

bool TargetStatusModel::apply(const QString& profile_id, const QString& payload) {
    const auto target = btrfsbackup::kde::parse_target_status(payload);
    if (!target.has_value() || target->profile_id != profile_id) {
        return false;
    }

    name_ = target->target_name;
    state_ = target->state;
    connected_ = target->connected;
    unlocked_ = target->unlocked;
    mounted_ = target->mounted;
    safe_to_remove_ = target->safe_to_remove;
    resetStorage();
    if (storage_supported_ && target->storage.has_value()) {
        storage_known_ = true;
        capacity_bytes_ = target->storage->capacity_bytes;
        used_bytes_ = target->storage->used_bytes;
        available_bytes_ = target->storage->available_bytes;
        usage_percent_ = target->storage->usage_percent;
        storage_live_ = target->storage->live;
        storage_measured_at_ = target->storage->measured_at;
        space_below_minimum_ = target->storage->space_state == QStringLiteral("below-configured-minimum");
    }
    emit changed();
    return true;
}

void TargetStatusModel::reset() {
    name_.clear();
    state_ = QStringLiteral("unknown");
    connected_ = false;
    unlocked_ = false;
    mounted_ = false;
    safe_to_remove_ = false;
    resetStorage();
    emit changed();
}

void TargetStatusModel::resetStorage() {
    storage_known_ = false;
    capacity_bytes_ = 0;
    used_bytes_ = 0;
    available_bytes_ = 0;
    usage_percent_ = -1;
    storage_live_ = false;
    storage_measured_at_.clear();
    space_below_minimum_ = false;
}
