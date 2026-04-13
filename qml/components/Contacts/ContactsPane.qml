import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common

Item {
    id: root

    required property var systemPalette
    property var allContacts: []

    property int selectedIndex: -1
    readonly property var selectedContact: {
        if (selectedIndex >= 0 && selectedIndex < sortedContacts.length)
            return sortedContacts[selectedIndex]
        return null
    }

    readonly property var sortedContacts: {
        if (!allContacts || allContacts.length === 0) { return [] }
        var arr = []
        for (var i = 0; i < allContacts.length; i++) {
            var c = allContacts[i]
            var name = c.displayName || ""
            var letter = name.length > 0 ? name[0].toUpperCase() : "#"
            if (letter < "A" || letter > "Z") { letter = "#" }
            var copy = {}
            for (var k in c) { copy[k] = c[k] }
            copy.sectionLetter = letter
            arr.push(copy)
        }
        return arr
    }

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ContactGroupsSidebar {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            contactCount: root.sortedContacts.length
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.2)
        }

        ContactList {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            contacts: root.sortedContacts
            selectedIndex: root.selectedIndex
            onContactSelected: (idx) => { root.selectedIndex = idx }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Qt.darker(Kirigami.Theme.backgroundColor, 1.2)
        }

        ContactDetailPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contact: root.selectedContact
        }
    }
}
