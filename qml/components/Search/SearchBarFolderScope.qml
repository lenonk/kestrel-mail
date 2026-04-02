import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../Common"

Item {
    id: scopeRoot

    // Required properties from parent SearchBar
    property string folderScopeText: ""
    property real folderScopeFixedWidth: 100
    property int folderScope: 2
    property int barHeight: 30

    // Emitted when the user picks a scope value
    signal scopeSelected(int newScope)
    // Emitted when the selector area is clicked (before opening the menu)
    signal clicked()

    Layout.alignment: Qt.AlignVCenter
    Layout.preferredWidth: scopeRoot.folderScopeFixedWidth
    Layout.preferredHeight: scopeRoot.barHeight

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        QQC2.Label {
            text: scopeRoot.folderScopeText
            font.pixelSize: 11
            color: Kirigami.Theme.textColor
            Layout.fillWidth: true
        }
        Kirigami.Icon {
            source: "arrow-down"
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            color: Kirigami.Theme.textColor
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            scopeRoot.clicked()
            folderScopeMenu.openIfClosed()
        }
    }

    PopupMenu {
        id: folderScopeMenu
        verticalOffset: 2

        QQC2.MenuItem {
            text: i18n("Current folder")
            checkable: true
            checked: scopeRoot.folderScope === 0
            onTriggered: scopeRoot.scopeSelected(0)
        }
        QQC2.MenuItem {
            text: i18n("Current folder and subfolders")
            checkable: true
            checked: scopeRoot.folderScope === 1
            onTriggered: scopeRoot.scopeSelected(1)
        }
        QQC2.MenuItem {
            text: i18n("All folders")
            checkable: true
            checked: scopeRoot.folderScope === 2
            onTriggered: scopeRoot.scopeSelected(2)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Custom Folder Selection...")
            enabled: false
        }
    }

    function openMenu() {
        folderScopeMenu.openIfClosed()
    }
}
