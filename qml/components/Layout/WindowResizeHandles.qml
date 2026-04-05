import QtQuick

Item {
    id: root

    required property int grabSize
    required property var targetWindow

    anchors.fill: parent
    z: 10000

    // Corners first so diagonal resize works naturally.
    Rectangle {
        color: "transparent"
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeFDiagCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeFDiagCursor
            onPressed: root.targetWindow.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeBDiagCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeBDiagCursor
            onPressed: root.targetWindow.startSystemResize(Qt.TopEdge | Qt.RightEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeBDiagCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeBDiagCursor
            onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeFDiagCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeFDiagCursor
            onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.grabSize
        anchors.bottomMargin: root.grabSize
        width: root.grabSize
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeHorCursor
            onPressed: root.targetWindow.startSystemResize(Qt.LeftEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.grabSize
        anchors.bottomMargin: root.grabSize
        width: root.grabSize
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeHorCursor
            onPressed: root.targetWindow.startSystemResize(Qt.RightEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.grabSize
        anchors.rightMargin: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeVerCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeVerCursor
            onPressed: root.targetWindow.startSystemResize(Qt.TopEdge)
        }
    }

    Rectangle {
        color: "transparent"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.grabSize
        anchors.rightMargin: root.grabSize
        height: root.grabSize
        HoverHandler { cursorShape: Qt.SizeVerCursor }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeVerCursor
            onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge)
        }
    }
}
