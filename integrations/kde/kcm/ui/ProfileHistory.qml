// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var historyModel
    required property var statusTextFor
    property var selectedEntry: ({})
    spacing: 0

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        visible: root.historyModel !== null && root.historyModel.errorCode.length > 0
        type: Kirigami.MessageType.Error
        text: root.historyErrorText()
        actions: [
            Kirigami.Action {
                text: translations.i18n("Retry")
                icon.name: "view-refresh-symbolic"
                onTriggered: root.historyModel.loadFirstPage()
            }
        ]
    }

    Repeater {
        model: root.historyModel?.entries ?? []

        delegate: QQC2.ItemDelegate {
            id: historyRow
            required property var modelData

            Layout.fillWidth: true
            onClicked: {
                root.selectedEntry = modelData;
                detailsDialog.open();
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: root.stateIcon(historyRow.modelData.state)
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }
                Kirigami.TitleSubtitle {
                    Layout.fillWidth: true
                    title: root.statusTextFor(historyRow.modelData.state)
                    subtitle: root.historySummary(historyRow.modelData)
                    selected: false
                }
                QQC2.Label {
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 11
                    text: root.dateTime(historyRow.modelData.finishedAt, translations.i18n("Unknown"))
                    elide: Text.ElideRight
                    opacity: 0.7
                }
                Kirigami.Icon {
                    source: historyRow.mirrored ? "go-previous-symbolic" : "go-next-symbolic"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: implicitWidth
                }
            }
        }
    }

    Kirigami.PlaceholderMessage {
        Layout.fillWidth: true
        visible: root.historyModel !== null && !root.historyModel.loading && root.historyModel.errorCode.length === 0 && root.historyModel.entries.length === 0
        icon.name: "view-history-symbolic"
        text: translations.i18n("No synchronization history")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.bottomMargin: Kirigami.Units.largeSpacing
        visible: root.historyModel !== null && (root.historyModel.loading || root.historyModel.hasMore)

        Item {
            Layout.fillWidth: true
        }
        QQC2.BusyIndicator {
            running: root.historyModel?.loading ?? false
            visible: running
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: implicitWidth
        }
        QQC2.Button {
            text: translations.i18n("Load more")
            icon.name: "arrow-down-symbolic"
            visible: root.historyModel?.hasMore ?? false
            enabled: !(root.historyModel?.loading ?? false)
            onClicked: root.historyModel.loadMore()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    QQC2.Dialog {
        id: detailsDialog
        parent: root.QQC2.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent !== null ? parent.width - Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 30, Kirigami.Units.gridUnit * 30)
        modal: true
        title: translations.i18n("Synchronization details")
        standardButtons: QQC2.Dialog.Close

        contentItem: Kirigami.FormLayout {
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Result:")
                text: root.statusTextFor(root.selectedEntry.state)
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Started:")
                text: root.dateTime(root.selectedEntry.startedAt, translations.i18n("Unknown"))
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Finished:")
                text: root.dateTime(root.selectedEntry.finishedAt, translations.i18n("Unknown"))
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Duration:")
                text: root.formatDuration(root.selectedEntry.durationSeconds)
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Sources:")
                text: String(root.selectedEntry.sourceCount ?? 0)
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Data transferred:")
                text: root.selectedEntry.bytesTransferredText || translations.i18n("Unknown")
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Average speed:")
                text: root.selectedEntry.averageSpeedText || translations.i18n("Unknown")
            }
            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Error code:")
                visible: (root.selectedEntry.errorCode?.length ?? 0) > 0
                text: root.selectedEntry.errorCode ?? ""
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }
        }
    }

    function stateIcon(state) {
        switch (state) {
        case "succeeded":
            return "dialog-positive";
        case "failed":
            return "dialog-error";
        case "cancelled":
            return "dialog-cancel";
        default:
            return "dialog-information";
        }
    }

    function historySummary(entry) {
        const parts = [];
        if (entry.durationSeconds >= 0)
            parts.push(root.formatDuration(entry.durationSeconds));
        if (entry.bytesTransferredText?.length > 0)
            parts.push(entry.bytesTransferredText);
        if (entry.sourceCount > 0)
            parts.push(translations.i18np("1 source", "%1 sources", entry.sourceCount));
        if (entry.errorCode?.length > 0)
            parts.push(entry.errorCode);
        return parts.join(" · ");
    }

    function formatDuration(value) {
        const seconds = Number(value);
        if (!isFinite(seconds) || seconds < 0)
            return translations.i18n("Unknown");
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const remainder = Math.floor(seconds % 60);
        if (hours > 0)
            return translations.i18n("%1 h %2 min", hours, minutes);
        if (minutes > 0)
            return translations.i18n("%1 min %2 sec", minutes, remainder);
        return translations.i18np("1 second", "%1 seconds", Math.max(1, remainder));
    }

    function dateTime(value, fallback) {
        const parsed = Date.parse(value);
        return isNaN(parsed) ? fallback : Qt.formatDateTime(new Date(parsed), Locale.ShortFormat);
    }

    function historyErrorText() {
        const code = root.historyModel?.errorCode ?? "";
        if (code === "manager.unsupported-history")
            return translations.i18n("The backup manager does not provide compatible backup history.");
        if (code === "manager.invalid-history")
            return translations.i18n("The backup manager returned invalid backup history.");
        return translations.i18n("Could not load backup history.");
    }
}
