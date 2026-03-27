import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    // External interface
    property var appRoot: null
    property int inactiveWidth: Kirigami.Units.gridUnit * 22
    property int activeWidth: Kirigami.Units.gridUnit * 30
    property int barHeight: Kirigami.Units.gridUnit + 10

    // State
    property bool editing: false
    property bool searchActive: false
    property string searchText: ""
    property int folderScope: 2   // 0=current, 1=subfolders, 2=all, 3=custom
    property int fieldScope: 1    // default: subject+sender+recipients+body

    // Signals
    signal searchRequested(string query)
    signal searchCleared()

    implicitWidth: barRect.width
    implicitHeight: barHeight

    readonly property var folderScopeLabels: [
        i18n("Current folder"),
        i18n("Current folder and subfolders"),
        i18n("All folders"),
        i18n("Custom Folder Selection...")
    ]

    readonly property var fieldScopeLabels: [
        i18n("Subject, sender, recipients, body, notes and attachments"),
        i18n("Subject, sender, recipients and body"),
        i18n("Subject, sender and recipients"),
        i18n("Subject"),
        i18n("Body"),
        i18n("Sender"),
        i18n("Recipients")
    ]

    readonly property string folderScopeText: root.folderScope === 0 ? i18n("Current folder")
                                            : root.folderScope === 1 ? i18n("Subfolders")
                                            : i18n("All folders")

    // Measure the widest folder scope display label dynamically
    readonly property var folderScopeDisplayLabels: [
        i18n("Current folder"),
        i18n("Subfolders"),
        i18n("All folders")
    ]
    property real folderScopeFixedWidth: 100 // default, computed on load
    FontMetrics {
        id: folderScopeFM
        font.pixelSize: 11
    }
    Component.onCompleted: {
        let maxW = 0
        for (let i = 0; i < folderScopeDisplayLabels.length; ++i) {
            const w = folderScopeFM.advanceWidth(folderScopeDisplayLabels[i])
            if (w > maxW) maxW = w
        }
        folderScopeFixedWidth = maxW + 12 + 6
    }

    function activateSearch() {
        const q = searchField.text.trim()
        if (q.length === 0) return
        root.searchText = q
        root.searchActive = true
        // Keep textScrolled true so inactive text doesn't flash back in
        root.editing = false
        searchField.focus = false
        recentSearchesPopup.close()
        // Re-trigger the delayed scroll for the active layout
        textScrollTimer.restart()
        if (root.appRoot && root.appRoot.dataStoreObj)
            root.appRoot.dataStoreObj.addRecentSearch(q)
        root.searchRequested(q)
    }

    function clearSearch() {
        root.searchText = ""
        searchField.text = ""
        recentSearchesPopup.close()
        // Set editing BEFORE clearing searchActive so barExpanded never goes false
        root.editing = true
        root.searchActive = false
        root.searchCleared()
        focusTimer.restart()
    }

    function enterEditing() {
        root.editing = true
        searchField.text = root.searchActive ? root.searchText : ""
        focusTimer.restart()
        if (root.searchActive) selectAllTimer.restart()
        refreshRecentSearches()
    }

    Timer {
        id: selectAllTimer
        interval: 50 // after focus is set
        onTriggered: searchField.selectAll()
    }

    // Short delay so the layout is visible before we push focus
    Timer {
        id: focusTimer
        interval: 30
        onTriggered: searchField.forceActiveFocus()
    }

    function exitEditing() {
        if (root.searchActive) {
            root.editing = false
            searchField.focus = false
        } else {
            root.editing = false
            searchField.text = ""
            searchField.focus = false
        }
        recentSearchesPopup.close()
    }

    property var recentSearchItems: []

    function refreshRecentSearches() {
        if (root.appRoot && root.appRoot.dataStoreObj) {
            root.recentSearchItems = root.appRoot.dataStoreObj.recentSearches(5)
        }
    }

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

    // Sequenced text scroll animation.
    // Expanding: width resizes (150ms) → text scrolls in (300ms)
    // Collapsing: text scrolls out (300ms) → width shrinks (150ms)
    property bool textScrolled: false
    property bool animateTextScroll: false
    property bool collapseHoldWidth: false // keeps width expanded while text scrolls out

    Timer {
        id: textScrollTimer
        interval: 160 // just after the 150ms width animation
        onTriggered: {
            root.animateTextScroll = true
            root.textScrolled = true
            animateOffTimer.restart()
        }
    }
    Timer {
        id: animateOffTimer
        interval: 350 // slightly longer than the 300ms scroll animation
        onTriggered: root.animateTextScroll = false
    }
    Timer {
        id: collapseWidthTimer
        interval: 10 // shrink width immediately on collapse
        onTriggered: {
            root.collapseHoldWidth = false // width starts shrinking (150ms)
            collapseScrollTimer.restart()
        }
    }
    Timer {
        id: collapseScrollTimer
        interval: 160 // after the 150ms width animation
        onTriggered: {
            root.animateTextScroll = true
            root.textScrolled = false // scroll placeholder back in
            collapseAnimOffTimer.restart()
        }
    }
    Timer {
        id: collapseAnimOffTimer
        interval: 350 // after the 300ms scroll animation
        onTriggered: root.animateTextScroll = false
    }

    readonly property bool barExpanded: root.editing || root.searchActive
    onBarExpandedChanged: {
        if (barExpanded && !textScrolled) {
            // Expanding from inactive — delay text scroll until width finishes
            collapseScrollTimer.stop()
            collapseWidthTimer.stop()
            collapseHoldWidth = false
            textScrollTimer.restart()
        } else if (!barExpanded) {
            // Collapsing — shrink width first, then scroll text back in
            textScrollTimer.stop()
            animateOffTimer.stop()
            collapseHoldWidth = true
            collapseWidthTimer.restart()
        }
    }

    // Close editing when clicking outside
    Connections {
        target: root.appRoot || null
        ignoreUnknownSignals: true
        function onSelectedFolderKeyChanged() {
            if (root.editing) root.exitEditing()
        }
    }

    // -----------------------------------------------------------------------
    // Main bar rectangle
    // -----------------------------------------------------------------------
    Rectangle {
        id: barRect
        anchors.verticalCenter: parent.verticalCenter
        width: (root.editing || root.searchActive || root.collapseHoldWidth) ? root.activeWidth : root.inactiveWidth
        height: root.barHeight
        radius: height / 2
        clip: true
        color: Qt.lighter(Kirigami.Theme.backgroundColor, 1.06)
        border.width: (root.editing || root.searchActive) ? 2 : 1
        function colorsAreSimilar(a, b) {
            return Math.abs(a.r - b.r) < 0.05
                && Math.abs(a.g - b.g) < 0.05
                && Math.abs(a.b - b.b) < 0.05
        }

        border.color: root.searchActive
                      ? (barRect.colorsAreSimilar(Kirigami.Theme.neutralTextColor, Kirigami.Theme.highlightColor)
                         ? Qt.lighter(Kirigami.Theme.negativeTextColor, 1.5)
                         : Kirigami.Theme.neutralTextColor)
                      : root.editing
                        ? Kirigami.Theme.highlightColor
                        : Kirigami.Theme.disabledTextColor

        Behavior on width {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
        Behavior on border.width {
            NumberAnimation { duration: 100 }
        }

        // =================================================================
        // Inactive state — slides UP when entering editing
        // =================================================================
        RowLayout {
            id: inactiveLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Kirigami.Units.smallSpacing + 1
            anchors.rightMargin: Kirigami.Units.smallSpacing + 1
            height: root.barHeight
            spacing: 2
            y: root.textScrolled ? -root.barHeight : 0

            Behavior on y {
                enabled: root.animateTextScroll
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            Kirigami.Icon {
                source: "edit-find"
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                color: Kirigami.Theme.disabledTextColor
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: i18n("Search (type '?' for help)")
                font.pixelSize: 12
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }
        }

        // Click overlay for inactive state
        MouseArea {
            anchors.fill: inactiveLayout
            visible: inactiveLayout.y === 0
            onClicked: root.enterEditing()
        }

        // =================================================================
        // Editing state — slides IN from below
        // =================================================================
        RowLayout {
            id: editingLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            height: root.barHeight
            spacing: 0
            y: (root.editing && root.textScrolled) ? 0 : root.barHeight

            Behavior on y {
                enabled: root.animateTextScroll
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            // Folder scope text + chevron — fixed width for longest option
            Item {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: root.folderScopeFixedWidth
                Layout.preferredHeight: root.barHeight

                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    QQC2.Label {
                        text: root.folderScopeText
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
                    id: folderScopeMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: folderScopeMenu.openIfClosed()
                }

                PopupMenu {
                    id: folderScopeMenu
                    verticalOffset: 2

                    QQC2.MenuItem {
                        text: i18n("Current folder")
                        checkable: true
                        checked: root.folderScope === 0
                        onTriggered: root.folderScope = 0
                    }
                    QQC2.MenuItem {
                        text: i18n("Current folder and subfolders")
                        checkable: true
                        checked: root.folderScope === 1
                        onTriggered: root.folderScope = 1
                    }
                    QQC2.MenuItem {
                        text: i18n("All folders")
                        checkable: true
                        checked: root.folderScope === 2
                        onTriggered: root.folderScope = 2
                    }
                    QQC2.MenuSeparator {}
                    QQC2.MenuItem {
                        text: i18n("Custom Folder Selection...")
                        enabled: false
                    }
                }
            }

            // Vertical divider after folder scope
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.barHeight - 10
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.5
            }

            // Search text field
            QQC2.TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                placeholderText: i18n("Start search")
                font.pixelSize: 12
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                implicitHeight: root.barHeight
                background: Item {}
                color: Kirigami.Theme.textColor
                placeholderTextColor: Kirigami.Theme.disabledTextColor
                cursorVisible: activeFocus

                Keys.onReturnPressed: root.activateSearch()
                Keys.onEnterPressed: root.activateSearch()
                Keys.onEscapePressed: {
                    if (root.searchActive) {
                        root.clearSearch()
                    } else {
                        root.exitEditing()
                    }
                }

                onTextChanged: {
                    if (text.length === 0) {
                        root.refreshRecentSearches()
                        if (root.recentSearchItems.length > 0 && root.editing) {
                            recentSearchesPopup.open()
                        }
                    } else {
                        recentSearchesPopup.close()
                    }
                }

                onActiveFocusChanged: {
                    if (activeFocus && root.editing) {
                        root.refreshRecentSearches()
                        if (root.recentSearchItems.length > 0 && searchField.text.length === 0) {
                            recentSearchesPopup.open()
                        }
                    }
                }
            }

            // Clear (X) button — visible when re-editing an active search
            Item {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Layout.alignment: Qt.AlignVCenter
                visible: root.searchActive

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "dialog-close"
                    color: Kirigami.Theme.negativeTextColor
                }
                MouseArea {
                    id: editClearBtnMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.clearSearch()
                }
            }

            // Magnifying glass button (field scope)
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignVCenter

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: "edit-find"
                    color: fieldScopeMA.containsMouse
                           ? Kirigami.Theme.textColor
                           : Kirigami.Theme.disabledTextColor
                }
                MouseArea {
                    id: fieldScopeMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: fieldScopeMenu.openIfClosed()
                }

                PopupMenu {
                    id: fieldScopeMenu
                    verticalOffset: 2

                    QQC2.MenuSeparator {}

                    Repeater {
                        model: root.fieldScopeLabels
                        delegate: QQC2.MenuItem {
                            required property int index
                            required property string modelData
                            text: modelData
                            checkable: true
                            checked: root.fieldScope === index
                            onTriggered: root.fieldScope = index
                        }
                    }

                    QQC2.MenuSeparator {}
                    QQC2.MenuItem {
                        text: i18n("Use server search if available")
                        enabled: false
                    }
                    QQC2.MenuItem {
                        text: i18n("Create Search Folder...")
                        enabled: false
                    }
                }
            }

            // Separator between magnifying glass and filter icon
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.barHeight - 12
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.4
            }

            // Filter / sliders button
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 4

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: "configure"
                    color: filterBtnMA.containsMouse
                           ? Kirigami.Theme.textColor
                           : Kirigami.Theme.disabledTextColor
                }
                MouseArea {
                    id: filterBtnMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        filterPopup.anchorItem = parent
                        filterPopup.visible = !filterPopup.visible
                    }
                }
            }
        }

        // =================================================================
        // Active search state — slides IN from below (when search is active but not editing)
        // =================================================================
        RowLayout {
            id: activeLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            height: root.barHeight
            spacing: 0
            y: (root.searchActive && !root.editing && root.textScrolled) ? 0 : root.barHeight

            Behavior on y {
                enabled: root.animateTextScroll
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            // Folder scope text + chevron — fixed width, matching editing layout
            Item {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: root.folderScopeFixedWidth
                Layout.preferredHeight: root.barHeight

                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    QQC2.Label {
                        text: root.folderScopeText
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
                    id: activeFolderMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.enterEditing()
                        folderScopeMenu.openIfClosed()
                    }
                }
            }

            // Vertical divider
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.barHeight - 10
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.5
            }

            // Search text display — use a read-only TextField to match editing layout metrics exactly
            QQC2.TextField {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.searchText
                font.pixelSize: 12
                readOnly: true
                color: Kirigami.Theme.textColor
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                implicitHeight: root.barHeight
                background: Item {}

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: root.enterEditing()
                }
            }

            // Clear (X) button
            Item {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Layout.alignment: Qt.AlignVCenter

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "dialog-close"
                    color: Kirigami.Theme.negativeTextColor
                }
                MouseArea {
                    id: clearBtnMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.clearSearch()
                }
            }

            // Separator between X and filter icon
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.barHeight - 12
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.4
            }

            // Filter icon
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 4

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: "configure"
                    color: activeFilterMA.containsMouse
                           ? Kirigami.Theme.textColor
                           : Kirigami.Theme.disabledTextColor
                }
                MouseArea {
                    id: activeFilterMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        filterPopup.anchorItem = parent
                        filterPopup.visible = !filterPopup.visible
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Recent searches dropdown
    // -----------------------------------------------------------------------
    QQC2.Popup {
        id: recentSearchesPopup
        // Align with the area between the two separators (folder scope divider to magnifying glass divider)
        readonly property real fieldStartX: barRect.x + 8 + root.folderScopeFixedWidth + 6 + 1 + 6
        readonly property real fieldEndX: barRect.x + barRect.width - 4 - 24 - 2 - 1 - 2 // rightMargin + filterBtn + separator(margins+width)
        x: fieldStartX - 7
        y: barRect.y + barRect.height + 2
        width: fieldEndX - fieldStartX + 8
        padding: 0
        modal: false
        closePolicy: QQC2.Popup.CloseOnPressOutside | QQC2.Popup.CloseOnEscape

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
                model: root.recentSearchItems

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
                                text: root.timeAgo((modelData && modelData.searchedAt) ? modelData.searchedAt : "")
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
                                    if (root.appRoot && root.appRoot.dataStoreObj && modelData && modelData.query)
                                        root.appRoot.dataStoreObj.removeRecentSearch(modelData.query)
                                    root.refreshRecentSearches()
                                    if (root.recentSearchItems.length === 0)
                                        recentSearchesPopup.close()
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
                            if (q.length > 0) {
                                searchField.text = q
                                root.activateSearch()
                            }
                        }
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Advanced filter popup
    // -----------------------------------------------------------------------
    SearchFilterPopup {
        id: filterPopup
        visible: false
        appRoot: root.appRoot
        onSearchTriggered: function(filterQuery) {
            if (filterQuery.length > 0) {
                searchField.text = filterQuery
                root.activateSearch()
            }
        }
    }

    // -----------------------------------------------------------------------
    // Global click-away handler
    // -----------------------------------------------------------------------
    Connections {
        target: (root.editing && root.appRoot) ? root.appRoot : null
        ignoreUnknownSignals: true
        function onSelectedMessageKeyChanged() {
            if (root.editing) root.exitEditing()
        }
    }
}
