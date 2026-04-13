import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../Common" as Common
import "../Calendar" as Calendar

Rectangle {
    id: rightPane

    required property var appRoot
    required property var systemPalette

    color: Kirigami.Theme.backgroundColor

    onWidthChanged: {
        var collapseThreshold = appRoot.rightCollapsedRailWidth + 2

        if (appRoot.rightPaneVisible && width > collapseThreshold) {
            appRoot.rightPaneExpandedWidth = width
        }

        if (!appRoot.paneAutoToggleEnabled) return

        if (appRoot.rightPaneVisible) {
            if (width <= collapseThreshold) {
                appRoot.rightPaneHiddenByButton = false
                appRoot.rightPaneVisible = false
            }
        } else {
            if (width > collapseThreshold) {
                appRoot.rightPaneExpandedWidth = width
                appRoot.rightPaneVisible = true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: appRoot.panelMargin
        spacing: appRoot.sectionSpacing
        visible: appRoot.rightPaneVisible

        PaneHeaderBar {
            title: i18n("Invites")
            titleBold: true
            titlePointSizeDelta: 0
            headerHeight: appRoot.folderHeaderHeight
            utilityIconSize: appRoot.headerUtilityIconSize
            utilityHighlightColor: rightPane.systemPalette.highlight
            collapseIconName: "go-next-symbolic"
            collapseIconSize: 24
            collapseButtonSize: 44
            collapseRightMargin: -15
            rowLeftMargin: 2
            rowRightMargin: 2
            onCollapseRequested: {
                appRoot.rightPaneHiddenByButton = true
                appRoot.rightPaneVisible = false
            }
        }

        QQC2.Label { text: i18n("All Accounts") }

        Item { Layout.preferredHeight: 4 }

        Common.PaneDivider {
            Layout.leftMargin: -appRoot.panelMargin
            Layout.rightMargin: -appRoot.panelMargin
        }

        Repeater {
            id: invitesRepeater
            model: appRoot.upcomingInvites
            delegate: Calendar.InviteCard {
                function formatInviteDate(isoStr, subtitle) {
                    if (!isoStr) { return subtitle || "" }
                    var d = new Date(isoStr)
                    var formatted = d.toLocaleDateString(Qt.locale(), "dddd, MMMM d, yyyy")
                    if (subtitle && subtitle !== "All day") {
                        formatted += " " + subtitle
                    } else if (subtitle === "All day") {
                        formatted += ", " + subtitle
                    }
                    return formatted
                }
                title: modelData.title || ""
                whenText: formatInviteDate(modelData.startIso, modelData.subtitle)
                fromText: modelData.organizer ? i18n("from %1", modelData.organizer) : ""
                accentColor: modelData.color || ""
                responseStatus: modelData.selfResponseStatus || ""
                sectionSpacing: appRoot.sectionSpacing
                radiusValue: appRoot.defaultRadius
                onAccepted: appRoot.imapServiceObj.respondToCalendarInvite(modelData.calendarId, modelData.eventId, "accepted")
                onDeclined: appRoot.imapServiceObj.respondToCalendarInvite(modelData.calendarId, modelData.eventId, "declined")
                onTentative: appRoot.imapServiceObj.respondToCalendarInvite(modelData.calendarId, modelData.eventId, "tentative")
            }
        }

        Item { Layout.fillHeight: true }

        Common.PaneDivider {
            Layout.leftMargin: -appRoot.panelMargin
            Layout.rightMargin: -appRoot.panelMargin
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: i18n("People"); toolTipText: i18n("People"); showLabel: false; active: appRoot.activeWorkspace === "people"; hoverFeedback: true; onClicked: appRoot.activeWorkspace = "people" }
            PaneIconButton { Layout.fillWidth: true; iconName: "task-complete"; label: i18n("Tasks"); toolTipText: i18n("Tasks"); showLabel: false; hoverFeedback: true }
            PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: i18n("Mail"); toolTipText: i18n("Mail"); showLabel: false; active: appRoot.activeWorkspace === "mail"; hoverFeedback: true; onClicked: appRoot.activeWorkspace = "mail" }
            PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: i18n("Calendar"); toolTipText: i18n("Calendar"); showLabel: false; active: appRoot.activeWorkspace === "calendar"; hoverFeedback: true; onClicked: appRoot.activeWorkspace = "calendar" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Kirigami.Units.smallSpacing + 1
        anchors.bottomMargin: Kirigami.Units.smallSpacing + 2
        visible: !appRoot.rightPaneVisible
        spacing: 0

        IconOnlyFlatButton {
            implicitWidth: parent.width
            implicitHeight: 44
            iconName: "go-previous-symbolic"
            iconSize: 24
            onClicked: {
                var minExpanded = 150
                if (!appRoot.rightPaneHiddenByButton) {
                    appRoot.rightPaneExpandedWidth = Math.max(appRoot.rightPaneExpandedWidth, minExpanded)
                }
                appRoot.rightPaneVisible = true
            }
        }

        Item { Layout.fillHeight: true }

        Common.PaneDivider {}

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            PaneIconButton { Layout.fillWidth: true; iconName: "user-identity"; label: ""; showLabel: false; toolTipText: i18n("People"); active: appRoot.activeWorkspace === "people"; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "people" }
            PaneIconButton { Layout.fillWidth: true; iconName: "task-complete"; label: ""; showLabel: false; toolTipText: i18n("Tasks"); hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4 }
            PaneIconButton { Layout.fillWidth: true; iconName: "mail-message"; label: ""; showLabel: false; toolTipText: i18n("Mail"); active: appRoot.activeWorkspace === "mail"; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "mail" }
            PaneIconButton { Layout.fillWidth: true; iconName: "office-calendar"; label: ""; showLabel: false; toolTipText: i18n("Calendar"); active: appRoot.activeWorkspace === "calendar"; hoverFeedback: true; underlineOnRight: true; sideIndicatorInset: 4; onClicked: appRoot.activeWorkspace = "calendar" }
        }
    }
}
