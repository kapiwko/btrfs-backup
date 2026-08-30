// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>

#include <iostream>

int main() {
    const QJsonObject metadata = QPluginLoader(QStringLiteral(BTRFSBACKUP_KIO_PLUGIN)).metaData();
    const QJsonObject protocols = metadata.value(QStringLiteral("MetaData")).toObject()
        .value(QStringLiteral("KDE-KIO-Protocols")).toObject();
    const QJsonObject protocol = protocols.value(QStringLiteral("btrfsbackup")).toObject();
    const bool valid = !protocol.isEmpty() && protocol.value(QStringLiteral("reading")).toBool() &&
        !protocol.value(QStringLiteral("writing")).toBool(true) &&
        !protocol.value(QStringLiteral("deleting")).toBool(true) &&
        !protocol.value(QStringLiteral("moving")).toBool(true) &&
        !protocol.value(QStringLiteral("makedir")).toBool(true) &&
        protocol.value(QStringLiteral("maxInstances")).toInt() > 0;
    if (!valid) {
        std::cerr << "KIO plugin does not declare a bounded read-only protocol\n";
        return 1;
    }
    std::cout << "KIO plugin metadata tests passed\n";
    return 0;
}
