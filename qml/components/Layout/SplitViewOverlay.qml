import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: overlay

    required property var appRoot
    required property Item messageListPane
    required property Item messageContentPane
    required property Item calendarPane
    required property Item rightPaneContainer

    anchors.fill: parent
    z: 50

    // Visual splitter lines (non-interactive).
    Rectangle {
        visible: overlay.messageListPane.visible
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        x: Math.round(overlay.messageListPane.x)
    }
    Rectangle {
        visible: overlay.appRoot.activeWorkspace === "mail" ? overlay.messageContentPane.visible : overlay.calendarPane.visible
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        x: Math.round(overlay.appRoot.activeWorkspace === "mail" ? overlay.messageContentPane.x : overlay.calendarPane.x)
    }
    Rectangle {
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.6)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        x: Math.round(overlay.rightPaneContainer.x)
    }

    // Drag handle: folder pane / message list divider
    Rectangle {
        color: "transparent"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: overlay.messageListPane.x - width / 2
        width: 8
        property real startWidth: 0
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        DragHandler {
            target: null
            onActiveChanged: {
                if (active) parent.startWidth = overlay.appRoot.folderPaneExpandedWidth
            }
            onTranslationChanged: {
                if (!active) return
                if (!overlay.appRoot.folderPaneVisible && translation.x > 2) {
                    overlay.appRoot.folderPaneVisible = true
                    overlay.appRoot.folderPaneHiddenByButton = false
                    parent.startWidth = Math.max(overlay.appRoot.folderPaneExpandedWidth, overlay.appRoot.collapsedRailWidth + 24)
                }
                if (!overlay.appRoot.folderPaneVisible) return
                overlay.appRoot.folderPaneExpandedWidth = Math.max(overlay.appRoot.collapsedRailWidth, parent.startWidth + translation.x)
            }
        }
    }

    // Drag handle: message list / content pane divider
    Rectangle {
        color: "transparent"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: overlay.messageContentPane.x
        width: 4
        property real startWidth: 0
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        DragHandler {
            target: null
            onActiveChanged: {
                if (active) parent.startWidth = overlay.appRoot.messageListPaneWidth
            }
            onTranslationChanged: {
                if (!active) return
                overlay.appRoot.messageListPaneWidth = Math.max(320, parent.startWidth + translation.x)
            }
        }
    }

    // Drag handle: content pane / right pane divider
    Rectangle {
        color: "transparent"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: overlay.rightPaneContainer.x - width / 2
        width: 8
        property real startWidth: 0
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        DragHandler {
            target: null
            onActiveChanged: {
                if (active) parent.startWidth = overlay.appRoot.rightPaneExpandedWidth
            }
            onTranslationChanged: {
                if (!active) return
                if (!overlay.appRoot.rightPaneVisible && translation.x < -2) {
                    overlay.appRoot.rightPaneVisible = true
                    overlay.appRoot.rightPaneHiddenByButton = false
                    parent.startWidth = Math.max(overlay.appRoot.rightPaneExpandedWidth, overlay.appRoot.rightCollapsedRailWidth + 24)
                }
                if (!overlay.appRoot.rightPaneVisible) return
                overlay.appRoot.rightPaneExpandedWidth = Math.max(overlay.appRoot.rightCollapsedRailWidth, parent.startWidth - translation.x)
            }
        }
    }
}
