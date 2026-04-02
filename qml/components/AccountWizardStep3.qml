import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: step3Root

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.topMargin: 16
        QQC2.Label { text: i18n("Finish") ; font.pixelSize: 22; font.bold: true }
        QQC2.Label { text: i18n("Review complete. Press Finish to connect and save this account.") }
    }
}
