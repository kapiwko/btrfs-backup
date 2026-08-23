import QtQuick 2.15
import QtQuick.Window 2.15
import org.btrfsbackup.plasma 1.0

Window {
    width: 1
    height: 1
    visible: false

    BackupStatusModel {
        id: status
        profile: "default"
    }

    Timer {
        interval: 10
        running: true
        repeat: false
        onTriggered: {
            if (status.profile !== "default") {
                Qt.exit(2)
            }
            Qt.exit(0)
        }
    }
}
