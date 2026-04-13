import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common

Rectangle {
    id: root

    property var contacts: []
    property int selectedIndex: -1

    signal contactSelected(int index)

    color: Kirigami.Theme.backgroundColor

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Kirigami.Theme.backgroundColor

            QQC2.Label {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: i18n("All Contacts (%1)", root.contacts.length)
                font.weight: Font.Medium
                opacity: 0.7
            }
        }

        Common.PaneDivider {}

        ListView {
            id: contactListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.contacts
            clip: true
            currentIndex: root.selectedIndex
            boundsBehavior: Flickable.StopAtBounds

            section.property: "sectionLetter"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: contactListView.width
                height: 28
                color: Qt.darker(Kirigami.Theme.backgroundColor, 1.05)

                QQC2.Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.section
                    font.bold: true
                    font.pixelSize: 13
                    opacity: 0.5
                }
            }

            delegate: ContactCard {
                displayName: modelData.displayName || ""
                subtitle: {
                    if (modelData.emails && modelData.emails.length > 0)
                        return modelData.emails[0].value || ""
                    if (modelData.phones && modelData.phones.length > 0)
                        return modelData.phones[0].value || ""
                    return ""
                }
                photoUrl: modelData.photoUrl || ""
                selected: index === root.selectedIndex
                onClicked: root.contactSelected(index)
            }

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                policy: QQC2.ScrollBar.AsNeeded
            }
        }
    }
}
