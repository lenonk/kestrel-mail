import QtQuick
import QtQuick.Window
import QtWebEngine
import org.kde.kirigami as Kirigami

Window {
    id: oauthWindow

    required property string oauthUrl
    required property Window parentWindow

    title: i18n("Google Sign-in")
    modality: Qt.ApplicationModal
    transientParent: parentWindow
    visible: false
    width: Math.min(Math.max(980, parentWindow.width + 80), Screen.desktopAvailableWidth - 120)
    height: Math.min(Math.max(720, parentWindow.height + 80), Screen.desktopAvailableHeight - 120)
    color: Qt.darker(Kirigami.Theme.backgroundColor, 1.06)

    onClosing: oauthWebView.stop()

    WebEngineView {
        id: oauthWebView
        anchors.fill: parent
        url: oauthWindow.oauthUrl
        zoomFactor: 1.0
        settings.localContentCanAccessRemoteUrls: true
        settings.autoLoadImages: true
        settings.javascriptCanOpenWindows: true
        settings.errorPageEnabled: true
    }
}
