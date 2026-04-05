import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

import "../Common" as Common
import "../Search" as Search

Rectangle {
    id: titleBar

    required property var appRoot
    required property var systemPalette

    property alias searchBar: searchBar

    color: Kirigami.Theme.backgroundColor

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: titleBar.appRoot.startSystemMove()
    }

    Item {
        anchors.fill: parent

        TitleBarIconButton {
            id: menuButton
            anchors.left: parent.left
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            buttonWidth: titleBar.appRoot.titleButtonWidth
            buttonHeight: titleBar.appRoot.titleButtonHeight
            iconSize: titleBar.appRoot.titleIconSize
            highlightColor: titleBar.systemPalette.highlight
            iconName: "open-menu-symbolic"
            onClicked: appMenu.openIfClosed()
        }

        QQC2.Label {
            anchors.left: menuButton.right
            anchors.leftMargin: Kirigami.Units.smallSpacing + 2
            anchors.verticalCenter: parent.verticalCenter
            text: i18n("Kestrel")
            font.bold: true
            font.pointSize: 14
        }

        Search.SearchBar {
            id: searchBar
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 5
            appRoot: titleBar.appRoot
            inactiveWidth: titleBar.appRoot.titleSearchWidth
            activeWidth: titleBar.appRoot.titleSearchWidth + Kirigami.Units.gridUnit * 8

            onSearchRequested: function(query) {
                if (titleBar.appRoot.messageListModelObj)
                    titleBar.appRoot.messageListModelObj.setSearchQuery(query)
            }
            onSearchCleared: {
                if (titleBar.appRoot.messageListModelObj)
                    titleBar.appRoot.messageListModelObj.setSearchQuery("")
                titleBar.appRoot.syncMessageListModelSelection()
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            TitleBarIconButton {
                buttonWidth: titleBar.appRoot.titleButtonWidth
                buttonHeight: titleBar.appRoot.titleButtonHeight
                iconSize: titleBar.appRoot.titleIconSize
                highlightColor: titleBar.systemPalette.highlight
                iconName: "window-minimize-symbolic"
                onClicked: titleBar.appRoot.showMinimized()
            }

            TitleBarIconButton {
                buttonWidth: titleBar.appRoot.titleButtonWidth
                buttonHeight: titleBar.appRoot.titleButtonHeight
                iconSize: titleBar.appRoot.titleIconSize
                highlightColor: titleBar.systemPalette.highlight
                iconName: titleBar.appRoot.visibility === Window.Maximized ? "window-restore-symbolic" : "window-maximize-symbolic"
                onClicked: titleBar.appRoot.visibility === Window.Maximized ? titleBar.appRoot.showNormal() : titleBar.appRoot.showMaximized()
            }

            TitleBarIconButton {
                buttonWidth: titleBar.appRoot.titleButtonWidth
                buttonHeight: titleBar.appRoot.titleButtonHeight
                iconSize: titleBar.appRoot.titleIconSize
                highlightColor: titleBar.systemPalette.highlight
                iconName: "window-close-symbolic"
                onClicked: Qt.quit()
            }
        }
    }

    Common.PopupMenu {
        id: appMenu
        parent: menuButton
        QQC2.MenuItem { text: i18n("Add Account..."); onTriggered: titleBar.appRoot.accountWizard.open() }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: i18n("Settings") }
        QQC2.MenuItem { text: i18n("Accounts") }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: i18n("Exit"); onTriggered: Qt.quit() }
    }
}
