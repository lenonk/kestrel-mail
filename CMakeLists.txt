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

set(CMAKE_CXX_STANDARD 20)
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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Sql Network Concurrent WebEngineQuick Widgets)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n)
find_package(KF6Kirigami REQUIRED)
find_package(KF6KirigamiPlatform REQUIRED)
find_package(Boost CONFIG REQUIRED)

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
    target_compile_options(mailio PRIVATE -fexceptions)
endif()
set_source_files_properties(src/core/mime/mailioparser.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions")

qt_standard_project_setup()

qt_add_executable(kestrel-mail
    src/main.cpp
    src/core/accounts/providerprofileservice.cpp
    src/core/accounts/accountrepository.cpp
    src/core/accounts/accountsetupcontroller.cpp
    src/core/auth/filetokenvault.cpp
    src/core/auth/oauthservice.cpp
    src/core/htmlprocessor.cpp
    src/core/store/datastore.cpp
    src/core/store/messagelistmodel.cpp
    src/core/transport/imap/imapservice.cpp
    src/core/transport/imap/parser/responseparser.cpp
    src/core/transport/imap/connection/imapio.cpp
    src/core/transport/imap/connection/imapconnection.cpp
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
        qml/components/AccountWizardDialog.qml
        qml/components/DarkLightToggleButton.qml
        qml/components/PaneIconButton.qml
        qml/components/TextToolTip.qml
        qml/components/MailActionButton.qml
        qml/components/FolderItemDelegate.qml
        qml/components/FolderSectionButton.qml
        qml/components/HeaderUtilityButton.qml
        qml/components/IconOnlyFlatButton.qml
        qml/components/PaneDivider.qml
        qml/components/PaneHeaderBar.qml
        qml/components/TitleBarIconButton.qml
        qml/components/PopupMenu.qml
        qml/components/AvatarBadge.qml
        qml/components/ContactHoverPopup.qml
        qml/components/InviteCard.qml
        qml/components/PaneIconStrip.qml
        qml/components/CollapsedPaneRail.qml
        qml/components/MessageList/MessageListPane.qml
        qml/components/MessageList/MessageCard.qml
        qml/components/MessageList/MessageCardAvatar.qml
        qml/components/MessageList/MessageCardIndicators.qml
        qml/components/MessageList/MessageCardStatusColumn.qml
        qml/components/MessageList/MessageCategoryButton.qml
        qml/components/MessageList/MessageListHeader.qml
        qml/components/MessageContent/MessageContentPane.qml
        qml/components/MessageContent/MessageInfoBars.qml
        qml/components/MessageContent/MessageHtmlView.qml
        qml/components/MessageContent/MessageThreadView.qml
        qml/components/Compose/Compose.qml
        qml/components/Calendar/CalendarPane.qml
        qml/components/Calendar/CalendarTopBar.qml
        qml/components/Calendar/CalendarWeekHeader.qml
        qml/components/Calendar/CalendarTimeGutter.qml
        qml/components/Calendar/CalendarWeekGrid.qml
        qml/components/Calendar/CalendarEventCard.qml
        qml/components/Calendar/CalendarSidebarPane.qml
        qml/components/Attachments/AttachmentCard.qml
        qml/components/Attachments/AttachmentHoverPopup.qml
        qml/important.svg
        qml/flag.svg
        qml/trash.svg
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
        KF6::Kirigami
        KF6::KirigamiPlatform
        mailio
)

qt_add_executable(kestrel-imap-lab
    src/imaplab_main.cpp
    src/tools/imaplabcontroller.cpp
    src/core/accounts/accountrepository.cpp
    src/core/auth/filetokenvault.cpp
)

qt_add_resources(kestrel-imap-lab kestrel_imap_lab_qml
    PREFIX "/"
    FILES
        qml/ImapLab.qml
)

target_link_libraries(kestrel-imap-lab
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
)

install(TARGETS kestrel-mail kestrel-imap-lab ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES kestrel-mail.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES assets/kestrel-mail.png
    DESTINATION ${KDE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
)
