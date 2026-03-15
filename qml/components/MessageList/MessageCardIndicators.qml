import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

RowLayout {
    id: root
    spacing: 4
    
    property int threadCount: 0
    property bool hasTrackingPixel: false
    property bool hasAttachments: false

    // Thread count pill
    Rectangle {
        visible: threadCount > 1
        implicitWidth: threadCountLabel.implicitWidth + 10
        implicitHeight: 18
        radius: 9
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.4)
        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.7)
        border.width: 1
        Layout.alignment: Qt.AlignVCenter

        QQC2.Label {
            id: threadCountLabel
            anchors.centerIn: parent
            text: threadCount.toString() + " >"
            font.pixelSize: 11
            font.bold: true
            color: Kirigami.Theme.textColor
            opacity: 0.85
        }
    }

    Kirigami.Icon {
        source: "crosshairs"
        Layout.preferredWidth: 18
        Layout.preferredHeight: 18
        opacity: 0.78
        visible: hasTrackingPixel
    }

    Kirigami.Icon {
        source: "mail-attachment"
        Layout.preferredWidth: 18
        Layout.preferredHeight: 18
        opacity: 0.75
        visible: hasAttachments
    }
}
