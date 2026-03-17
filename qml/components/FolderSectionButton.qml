import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: root

    property bool expanded: false
    property string sectionIcon: "folder"
    property string title: ""
    property real titleOpacity: 0.85
    property int rowHeight: 40
    property int chevronSize: 16
    property int sectionIconSize: 16
    property string rightStatusIcon: ""
    property string rightActivityIcon: ""
    property bool rightActivitySpinning: false

    signal activated()

    flat: true
    Layout.fillWidth: true
    implicitHeight: rowHeight
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: false
    background: Item {}

    contentItem: RowLayout {
        id: rowContent
        spacing: Kirigami.Units.smallSpacing

        property real spinAngle: 0
        NumberAnimation on spinAngle {
            from: 0
            to: 360
            duration: 1400
            loops: Animation.Infinite
            running: root.rightActivitySpinning
        }

        Kirigami.Icon {
            source: root.expanded ? "go-down-symbolic" : "go-next-symbolic"
            Layout.preferredWidth: root.chevronSize
            Layout.preferredHeight: root.chevronSize
        }

        Kirigami.Icon {
            source: root.sectionIcon
            Layout.preferredWidth: root.sectionIconSize
            Layout.preferredHeight: root.sectionIconSize
        }

        QQC2.Label {
            text: root.title
            opacity: root.titleOpacity
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        Kirigami.Icon {
            visible: !!root.rightActivityIcon
            source: root.rightActivityIcon
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            opacity: 0.9
            transform: Rotation {
                origin.x: 7
                origin.y: 7
                angle: rowContent.spinAngle
            }
        }

        Kirigami.Icon {
            visible: !!root.rightStatusIcon
            source: root.rightStatusIcon
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            opacity: 0.85
        }
    }

    onRightActivitySpinningChanged: {
        if (!rightActivitySpinning) rowContent.spinAngle = 0
    }

    onClicked: root.activated()
}
