import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var appRoot
    required property bool hasExternalImages
    required property bool hasTrackingPixel
    required property bool imagesAllowed
    required property bool isTrustedSender
    required property string listUnsubscribeUrl
    required property string senderDomain
    required property bool trackingAllowed
    required property string trackerVendor

    signal allowTracking()
    signal loadImagesAlways()
    signal loadImagesOnce()

    Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
    Layout.fillWidth: true
    spacing: Kirigami.Units.largeSpacing

    // Unsubscribe bar
    RowLayout {
        Layout.alignment: Qt.AlignLeft
        spacing: Kirigami.Units.smallSpacing
        visible: root.listUnsubscribeUrl.length > 0

        Kirigami.Icon {
            Layout.alignment: Qt.AlignLeft
            Layout.preferredHeight: 16
            Layout.preferredWidth: 16
            source: "help-contextual"
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: "#4ea3ff"
            font.bold: true
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("Unsubscribe")

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: Qt.openUrlExternally(root.listUnsubscribeUrl)
            }
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("from receiving these messages")
        }
    }

    // Tracker bar — shown when tracking is detected but not yet allowed
    RowLayout {
        Layout.alignment: Qt.AlignLeft
        spacing: Kirigami.Units.smallSpacing
        visible: root.hasTrackingPixel && !root.trackingAllowed && root.imagesAllowed

        Kirigami.Icon {
            Layout.alignment: Qt.AlignLeft
            Layout.preferredHeight: 16
            Layout.preferredWidth: 16
            source: "crosshairs"
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: "#4ea3ff"
            font.bold: true
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("Allow email tracking.")

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.allowTracking()
            }
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: (root.trackerVendor || "Email") + i18n(" tracking was blocked to preserve privacy.")
        }
    }

    // Images bar — hidden when a tracking pixel is present (tracker bar takes priority)
    RowLayout {
        Layout.alignment: Qt.AlignLeft
        spacing: Kirigami.Units.smallSpacing
        visible: root.hasExternalImages && !root.imagesAllowed && !root.isTrustedSender

        Kirigami.Icon {
            Layout.alignment: Qt.AlignLeft
            Layout.preferredHeight: 16
            Layout.preferredWidth: 16
            source: "messagebox_warning"
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: "#4ea3ff"
            font.bold: true
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("Download Pictures")

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.loadImagesOnce()
            }
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("or")
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: "#4ea3ff"
            font.bold: true
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("always download pictures from this sender.")

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.loadImagesAlways()
            }
        }
        QQC2.Label {
            Layout.fillWidth: false
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 2
            text: i18n("To preserve privacy, external content was not downloaded.")
        }
    }
}
