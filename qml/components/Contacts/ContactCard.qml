import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common

Rectangle {
    id: root

    property string displayName: ""
    property string subtitle: ""
    property string photoUrl: ""
    property bool selected: false

    signal clicked()

    width: ListView.view ? ListView.view.width : implicitWidth
    height: 56
    color: root.selected
           ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                     Kirigami.Theme.highlightColor.b, 0.15)
           : hover.hovered
             ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                       Kirigami.Theme.highlightColor.b, 0.07)
             : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Common.AvatarBadge {
            size: 40
            displayName: root.displayName
            avatarSources: root.photoUrl.length > 0 ? [root.photoUrl] : []
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            QQC2.Label {
                Layout.fillWidth: true
                text: root.displayName
                elide: Text.ElideRight
                font.weight: Font.Medium
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: root.subtitle
                elide: Text.ElideRight
                opacity: 0.6
                font.pixelSize: 12
                visible: root.subtitle.length > 0
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    HoverHandler { id: hover }
}
