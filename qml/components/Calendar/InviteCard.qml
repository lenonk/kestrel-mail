import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property string title: ""
    property string whenText: ""
    property string fromText: ""
    property string accentColor: ""
    property string responseStatus: ""
    property bool showBottomDivider: true
    property int sectionSpacing: 8
    property int radiusValue: 4

    signal accepted()
    signal declined()
    signal tentative()

    Layout.fillWidth: true
    implicitHeight: contentCol.implicitHeight + contentCol.anchors.topMargin + contentCol.anchors.bottomMargin
    radius: root.radiusValue
    color: Kirigami.Theme.backgroundColor
    border.color: "transparent"

    Rectangle {
        id: colorBar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        width: 3
        radius: 2
        color: root.accentColor.length > 0 ? root.accentColor : "transparent"
        visible: root.accentColor.length > 0
    }

    Column {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.accentColor.length > 0 ? root.sectionSpacing + 6 : root.sectionSpacing
        anchors.topMargin: root.sectionSpacing
        anchors.rightMargin: root.sectionSpacing
        anchors.bottomMargin: root.sectionSpacing
        spacing: root.sectionSpacing - 3

        QQC2.Label {
            text: root.title
            font.bold: true
        }

        QQC2.Label {
            text: root.whenText
            wrapMode: Text.Wrap
        }

        QQC2.Label {
            text: root.fromText
            wrapMode: Text.Wrap
            opacity: 0.8
            visible: root.fromText.length > 0
        }

        Item { width: 1; height: 4 }

        Row {
            spacing: 6

            Rectangle {
                width: 36
                height: 26
                radius: 4
                color: root.responseStatus === "accepted" ? "#2E7D32" : "#CFEEDB"
                border.width: root.responseStatus === "accepted" ? 0 : 1
                border.color: "#B5DEC7"
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "dialog-ok-apply"
                    width: 24; height: 24
                    color: root.responseStatus === "accepted" ? "white" : "#2E7D32"
                    isMask: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.responseStatus = "accepted"
                        root.accepted()
                    }
                }
            }

            Rectangle {
                width: 36
                height: 26
                radius: 4
                color: root.responseStatus === "declined" ? "#C62828" : "#F7D6D9"
                border.width: root.responseStatus === "declined" ? 0 : 1
                border.color: "#EABCC2"
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "dialog-cancel"
                    width: 24; height: 24
                    color: root.responseStatus === "declined" ? "white" : "#C62828"
                    isMask: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.responseStatus = "declined"
                        root.declined()
                    }
                }
            }

            Rectangle {
                width: 36
                height: 26
                radius: 4
                color: root.responseStatus === "tentative" ? "#F57F17" : "#F6EDC9"
                border.width: root.responseStatus === "tentative" ? 0 : 1
                border.color: "#E9DFAE"
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "question"
                    width: 24; height: 24
                    color: root.responseStatus === "tentative" ? "white" : "#F57F17"
                    isMask: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.responseStatus = "tentative"
                        root.tentative()
                    }
                }
            }
        }

        Item { width: 1; height: 8 }

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.7)
            visible: root.showBottomDivider
        }
    }
}
