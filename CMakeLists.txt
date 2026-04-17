cmake_minimum_required(VERSION 3.27)

# Prefer clang toolchain by default in this project.
if(NOT DEFINED ENV{CC})
    set(ENV{CC} "/usr/bin/clang")
endif()
if(NOT DEFINED ENV{CXX})
    set(ENV{CXX} "/usr/bin/clang++")
endif()
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE FILEPATH "C compiler" FORCE)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE FILEPATH "C++ compiler" FORCE)
endif()

project(kestrel-mail VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(QT_DEFAULT_MAJOR_VERSION 6)

# Default to RelWithDebInfo for single-config generators unless explicitly overridden.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Ensure release builds use aggressive optimization.
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
kde_enable_exceptions()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Sql Network Concurrent WebEngineQuick Widgets)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n Notifications Wallet)
find_package(KF6Kirigami REQUIRED)
find_package(KF6KirigamiPlatform REQUIRED)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBSECRET libsecret-1)
    pkg_check_modules(BOTAN REQUIRED IMPORTED_TARGET botan-3)
endif()

# Work around distro packaging where Kirigami QML plugin targets referenced by
# Qt's qml import scanner are not exported as CMake targets.
foreach(_kirigami_qml_plugin_target IN ITEMS
    KF6Kirigamiplugin
    KF6KirigamiPrivateplugin
    KF6KirigamiPolyfillplugin
    KF6::KirigamiPlatformplugin
    KF6KirigamiDialogsplugin
    KF6KirigamiLayoutsplugin
    KF6KirigamiLayoutsPrivateplugin
    KF6KirigamiControlsplugin
)
    if(NOT TARGET ${_kirigami_qml_plugin_target})
        add_library(${_kirigami_qml_plugin_target} INTERFACE IMPORTED)
    endif()
endforeach()

set(MAILIO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(MAILIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MAILIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/mailio)
if (TARGET mailio)
    target_compile_options(mailio PRIVATE -fexceptions -w)
endif()
set_source_files_properties(src/core/mime/mailioparser.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-Wno-overloaded-virtual")

qt_standard_project_setup()

qt_add_executable(kestrel-mail
    src/main.cpp
    src/ui/splashscreen.cpp
    src/ui/calendarlayouthelper.cpp
    src/core/accounts/providerprofileservice.cpp
    src/core/accounts/accountrepository.cpp
    src/core/accounts/accountsetupcontroller.cpp
    src/core/accounts/iaccount.cpp
    src/core/accounts/accountmanager.cpp
    src/core/accounts/baseaccount.cpp
    src/core/accounts/gmailaccount.cpp
    src/core/accounts/imapaccount.cpp
    src/core/auth/filetokenvault.cpp
    src/core/auth/kwallettokenvault.cpp
    $<$<BOOL:${LIBSECRET_FOUND}>:src/core/auth/libsecrettokenvault.cpp>
    src/core/auth/oauthservice.cpp
    src/core/htmlprocessor.cpp
    src/core/store/datastore.cpp
    src/core/store/contactstore.cpp
    src/core/crypto/pgpkeymanager.cpp
    src/core/store/folderstatsstore.cpp
    src/core/store/userprefsstore.cpp
    src/core/store/messagestore.cpp
    src/core/store/messagelistmodel.cpp
    src/core/transport/imap/imapservice.cpp
    src/core/transport/imap/googleapiservice.cpp
    src/core/transport/imap/parser/responseparser.cpp
    src/core/transport/imap/connection/imapio.cpp
    src/core/transport/imap/connection/imapconnection.cpp
    src/core/transport/imap/connection/connectionpool.cpp
    src/core/transport/imap/sync/syncutils.cpp
    src/core/transport/imap/sync/syncengine.cpp
    src/core/transport/imap/sync/idlewatcher.cpp
    src/core/transport/imap/sync/backgroundworker.cpp
    src/core/transport/imap/message/messagehydrator.cpp
    src/core/transport/imap/message/messageutils.cpp
    src/core/transport/imap/message/avatarresolver.cpp
    src/core/transport/imap/message/bodyprocessor.cpp
    src/core/mime/mailioparser.cpp
    src/core/transport/smtp/smtpservice.cpp
)

qt_add_resources(kestrel-mail kestrel_mail_qml
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/components/AccountWizard/AccountWizardDialog.qml
        qml/components/AccountWizard/AccountWizardStep1.qml
        qml/components/AccountWizard/AccountWizardStep2.qml
        qml/components/AccountWizard/AccountWizardStep3.qml
        qml/components/AccountWizard/OAuthBrowserWindow.qml
        qml/components/AccountWizard/AvatarChooserDialog.qml
        qml/components/AccountWizard/ManualIdentityStep.qml
        qml/components/AccountWizard/ManualIncomingStep.qml
        qml/components/AccountWizard/ManualOutgoingStep.qml
        qml/components/AccountWizard/ManualTestStep.qml
        qml/components/Attachments/AttachmentCard.qml
        qml/components/Attachments/AttachmentHoverPopup.qml
        qml/components/Calendar/CalendarPane.qml
        qml/components/Calendar/CalendarTopBar.qml
        qml/components/Calendar/CalendarWeekHeader.qml
        qml/components/Calendar/CalendarTimeGutter.qml
        qml/components/Calendar/CalendarWeekGrid.qml
        qml/components/Calendar/CalendarEventCard.qml
        qml/components/Calendar/CalendarAllDayRow.qml
        qml/components/Calendar/CalendarSidebarPane.qml
        qml/components/Calendar/InviteCard.qml
        qml/components/Contacts/ContactsPane.qml
        qml/components/Contacts/ContactGroupsSidebar.qml
        qml/components/Contacts/ContactList.qml
        qml/components/Contacts/ContactCard.qml
        qml/components/Contacts/ContactDetailPane.qml
        qml/components/Common/qmldir
        qml/components/Common/KestrelColors.qml
        qml/components/Common/ArrowPopup.qml
        qml/components/Common/AvatarBadge.qml
        qml/components/Common/BarberPoleProgressBar.qml
        qml/components/Common/PaneDivider.qml
        qml/components/Common/PopupMenu.qml
        qml/components/Common/StyledButton.qml
        qml/components/Common/TextToolTip.qml
        qml/components/Compose/Compose.qml
        qml/components/Layout/CollapsedPaneRail.qml
        qml/components/Layout/DarkLightToggleButton.qml
        qml/components/Layout/FolderItemDelegate.qml
        qml/components/Layout/FolderSectionButton.qml
        qml/components/Layout/HeaderUtilityButton.qml
        qml/components/Layout/IconOnlyFlatButton.qml
        qml/components/Layout/MailActionButton.qml
        qml/components/Layout/PaneHeaderBar.qml
        qml/components/Layout/PaneIconButton.qml
        qml/components/Layout/PaneIconStrip.qml
        qml/components/Layout/TitleBarIconButton.qml
        qml/components/Layout/WindowResizeHandles.qml
        qml/components/Layout/TitleBar.qml
        qml/components/Layout/MailToolbar.qml
        qml/components/Layout/FolderPane.qml
        qml/components/Layout/RightPane.qml
        qml/components/Layout/SplitViewOverlay.qml
        qml/utils/ColorUtilities.js
        qml/utils/FolderUtils.js
        qml/utils/DisplayUtils.js
        qml/utils/ComposeUtils.js
        qml/utils/MailActions.js
        qml/components/MessageContent/ContactHoverPopup.qml
        qml/components/MessageContent/MessageContentPane.qml
        qml/components/MessageContent/MessageInfoBars.qml
        qml/components/MessageContent/MessageHtmlView.qml
        qml/components/MessageContent/MessageThreadView.qml
        qml/components/MessageList/MessageListPane.qml
        qml/components/MessageList/MessageCard.qml
        qml/components/MessageList/MessageCardDragVisual.qml
        qml/components/MessageList/MessageCardAvatar.qml
        qml/components/MessageList/MessageCardIndicators.qml
        qml/components/MessageList/MessageCardStatusColumn.qml
        qml/components/MessageList/MessageCategoryButton.qml
        qml/components/MessageList/MessageListHeader.qml
        qml/components/Search/SearchBar.qml
        qml/components/Search/SearchBarFolderScope.qml
        qml/components/Search/RecentSearchesDropdown.qml
        qml/components/Search/SearchFilterPopup.qml
        qml/images/important.svg
        qml/images/flag.svg
        qml/images/trash.svg
        qml/images/gmail_account_icon.svg
        qml/images/account-avatars/avatar-01.svg
        qml/images/account-avatars/avatar-02.svg
        qml/images/account-avatars/avatar-03.svg
        qml/images/account-avatars/avatar-04.svg
        qml/images/account-avatars/avatar-05.svg
        qml/images/account-avatars/avatar-06.svg
        qml/images/account-avatars/avatar-07.svg
        qml/images/account-avatars/avatar-08.svg
        qml/images/account-avatars/avatar-09.svg
        qml/images/account-avatars/avatar-10.svg
        qml/images/account-avatars/avatar-11.svg
        qml/images/account-avatars/avatar-12.svg
        qml/images/account-avatars/avatar-13.svg
        qml/images/account-avatars/avatar-14.svg
        qml/images/account-avatars/avatar-15.svg
        qml/images/account-avatars/avatar-16.svg
        qml/images/account-avatars/avatar-17.svg
        qml/images/account-avatars/avatar-18.svg
)

set(KESTREL_PROVIDERS_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/providers.json")

set_source_files_properties(${KESTREL_PROVIDERS_RESOURCE}
    PROPERTIES
        QT_RESOURCE_ALIAS "providers.json"
)

qt_add_resources(kestrel-mail kestrel_mail_data
    PREFIX "/data"
    FILES
        ${KESTREL_PROVIDERS_RESOURCE}
        assets/kestrel-mail.png
        assets/splash.png
)

target_link_libraries(kestrel-mail
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Sql
        Qt6::Network
        Qt6::Concurrent
        Qt6::WebEngineQuick
        Qt6::Widgets
        KF6::I18n
        KF6::CoreAddons
        KF6::Notifications
        KF6::Wallet
        KF6::Kirigami
        KF6::KirigamiPlatform
        mailio
)

if(LIBSECRET_FOUND)
    target_compile_definitions(kestrel-mail PRIVATE HAVE_LIBSECRET)
    target_include_directories(kestrel-mail PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(kestrel-mail PRIVATE ${LIBSECRET_LIBRARIES})
endif()

target_link_libraries(kestrel-mail PRIVATE PkgConfig::BOTAN)

install(TARGETS kestrel-mail ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES kestrel-mail.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES data/kestrel-mail.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)

install(FILES assets/kestrel-mail.png
    DESTINATION ${KDE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
)
