import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Popup {
    id: dropdownRoot

    // External data
    property var recentSearchItems: []
    property var appRoot: null

    // Signals
    signal searchSelected(string query)
    signal itemDeleted(string query)

    padding: 0
    modal: false
    closePolicy: QQC2.Popup.CloseOnPressOutside | QQC2.Popup.CloseOnEscape

    function timeAgo(isoDateStr) {
        if (!isoDateStr || isoDateStr.length === 0) return ""
        const then = new Date(isoDateStr + "Z")
        const now = new Date()
        const diffMs = now - then
        const diffMin = Math.floor(diffMs / 60000)
        if (diffMin < 1) return i18n("Searched now")
        if (diffMin < 60) return i18n("Searched %1 min ago", diffMin)
        const diffHr = Math.floor(diffMin / 60)
        if (diffHr < 24) return i18n("Searched %1 hr ago", diffHr)
        const diffDays = Math.floor(diffHr / 24)
        return i18n("Searched %1 days ago", diffDays)
    }

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.2)
        radius: 4
    }

    contentItem: Column {
        width: parent ? parent.width : 0
        spacing: 0

        Repeater {
            model: dropdownRoot.recentSearchItems

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: parent.width
                height: 36
                color: recentItemMA.containsMouse
                       ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.15)
                       : "transparent"
                radius: index === 0 ? 4 : 0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8

                    Kirigami.Icon {
                        source: "edit-find"
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter
                        color: Kirigami.Theme.disabledTextColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            text: (modelData && modelData.query) ? modelData.query : ""
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        QQC2.Label {
                            text: dropdownRoot.timeAgo((modelData && modelData.searchedAt) ? modelData.searchedAt : "")
                            font.pixelSize: 10
                            opacity: 0.6
                            Layout.fillWidth: true
                        }
                    }

                    Item {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        Layout.alignment: Qt.AlignVCenter

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            source: "edit-delete-remove"
                            color: deleteMA.containsMouse
                                   ? Kirigami.Theme.negativeTextColor
                                   : Kirigami.Theme.disabledTextColor
                        }
                        MouseArea {
                            id: deleteMA
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                const q = (modelData && modelData.query) ? modelData.query : ""
                                if (q.length > 0)
                                    dropdownRoot.itemDeleted(q)
                            }
                        }
                    }
                }

                MouseArea {
                    id: recentItemMA
                    anchors.fill: parent
                    anchors.rightMargin: 30
                    hoverEnabled: true
                    onClicked: {
                        const q = (modelData && modelData.query) ? modelData.query : ""
                        if (q.length > 0)
                            dropdownRoot.searchSelected(q)
                    }
                }
            }
        }
    }
}
