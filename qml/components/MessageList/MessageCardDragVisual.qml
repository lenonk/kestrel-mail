import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: dragVisual

    property string senderName: ""
    property string subject: ""
    property string snippet: ""
    property int messageCount: 1
    property color highlightColor: "steelblue"

    width: 340
    implicitHeight: cardRect.implicitHeight
    height: implicitHeight
    clip: true

    Rectangle {
        id: cardRect
        width: parent.width
        implicitHeight: cardLayout.implicitHeight + 16
        radius: 6
        opacity: 0.92
        scale: 0.9
        transformOrigin: Item.Center

        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        border.width: 1
        border.color: dragVisual.highlightColor

        ColumnLayout {
            id: cardLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Kirigami.Icon {
                    source: "mail-message"
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: dragVisual.senderName
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    color: Kirigami.Theme.textColor
                }

                QQC2.Label {
                    visible: dragVisual.messageCount > 1
                    text: i18n("%1 messages", dragVisual.messageCount)
                    font.pixelSize: 11
                    opacity: 0.7
                    color: Kirigami.Theme.textColor
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: dragVisual.subject || i18n("(No subject)")
                font.pixelSize: 12
                elide: Text.ElideRight
                color: Kirigami.Theme.textColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: dragVisual.snippet
                font.pixelSize: 11
                elide: Text.ElideRight
                opacity: 0.6
                visible: text.length > 0
                color: Kirigami.Theme.textColor
            }
        }
    }
}
