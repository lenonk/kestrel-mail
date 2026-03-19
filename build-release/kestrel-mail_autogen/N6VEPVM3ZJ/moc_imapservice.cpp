/****************************************************************************
** Meta object code from reading C++ file 'imapservice.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/transport/imap/imapservice.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'imapservice.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN11ImapServiceE_t {};
} // unnamed namespace

template <> constexpr inline auto ImapService::qt_create_metaobjectdata<qt_meta_tag_ZN11ImapServiceE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ImapService",
        "syncFinished",
        "",
        "ok",
        "message",
        "syncActivityChanged",
        "active",
        "hydrateStatus",
        "realtimeStatus",
        "attachmentReady",
        "accountEmail",
        "uid",
        "partId",
        "localPath",
        "attachmentDownloadProgress",
        "progressPercent",
        "googleCalendarListChanged",
        "googleWeekEventsChanged",
        "syncAll",
        "announce",
        "syncFolder",
        "folderName",
        "refreshFolderList",
        "hydrateMessageBody",
        "moveMessage",
        "folder",
        "targetFolder",
        "markMessageRead",
        "initialize",
        "shutdown",
        "openAttachmentUrl",
        "url",
        "saveAttachmentUrl",
        "suggestedFileName",
        "attachmentsForMessage",
        "QVariantList",
        "openAttachment",
        "fileName",
        "encoding",
        "saveAttachment",
        "prefetchAttachments",
        "prefetchImageAttachments",
        "fileSha256",
        "dataUriSha256",
        "dataUri",
        "cachedAttachmentPath",
        "attachmentPreviewPath",
        "mimeType",
        "refreshGoogleCalendars",
        "refreshGoogleWeekEvents",
        "calendarIds",
        "weekStartIso",
        "weekEndIso",
        "googleCalendarList",
        "googleWeekEvents"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'syncFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'syncActivityChanged'
        QtMocHelpers::SignalData<void(bool)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Signal 'hydrateStatus'
        QtMocHelpers::SignalData<void(bool, const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'realtimeStatus'
        QtMocHelpers::SignalData<void(bool, const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'attachmentReady'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QString &, const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 }, { QMetaType::QString, 13 },
        }}),
        // Signal 'attachmentDownloadProgress'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QString &, int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 }, { QMetaType::Int, 15 },
        }}),
        // Signal 'googleCalendarListChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'googleWeekEventsChanged'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'syncAll'
        QtMocHelpers::MethodData<void(bool)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
        // Method 'syncAll'
        QtMocHelpers::MethodData<void()>(18, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'syncFolder'
        QtMocHelpers::MethodData<void(const QString &, bool)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 21 }, { QMetaType::Bool, 19 },
        }}),
        // Method 'syncFolder'
        QtMocHelpers::MethodData<void(const QString &)>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 21 },
        }}),
        // Method 'refreshFolderList'
        QtMocHelpers::MethodData<void(bool)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
        // Method 'refreshFolderList'
        QtMocHelpers::MethodData<void()>(22, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'hydrateMessageBody'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 },
        }}),
        // Method 'moveMessage'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &, const QString &)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 25 }, { QMetaType::QString, 11 }, { QMetaType::QString, 26 },
        }}),
        // Method 'markMessageRead'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 25 }, { QMetaType::QString, 11 },
        }}),
        // Method 'initialize'
        QtMocHelpers::MethodData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'shutdown'
        QtMocHelpers::MethodData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'openAttachmentUrl'
        QtMocHelpers::MethodData<void(const QString &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'saveAttachmentUrl'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(32, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 33 },
        }}),
        // Method 'saveAttachmentUrl'
        QtMocHelpers::MethodData<bool(const QString &)>(32, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'attachmentsForMessage'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QString &, const QString &)>(34, 2, QMC::AccessPublic, 0x80000000 | 35, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 },
        }}),
        // Method 'openAttachment'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 },
            { QMetaType::QString, 37 }, { QMetaType::QString, 38 },
        }}),
        // Method 'saveAttachment'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(39, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 },
            { QMetaType::QString, 37 }, { QMetaType::QString, 38 },
        }}),
        // Method 'prefetchAttachments'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 },
        }}),
        // Method 'prefetchImageAttachments'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 21 }, { QMetaType::QString, 11 },
        }}),
        // Method 'fileSha256'
        QtMocHelpers::MethodData<QString(const QString &) const>(42, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 13 },
        }}),
        // Method 'dataUriSha256'
        QtMocHelpers::MethodData<QString(const QString &) const>(43, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 44 },
        }}),
        // Method 'cachedAttachmentPath'
        QtMocHelpers::MethodData<QString(const QString &, const QString &, const QString &) const>(45, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 },
        }}),
        // Method 'attachmentPreviewPath'
        QtMocHelpers::MethodData<QString(const QString &, const QString &, const QString &, const QString &, const QString &)>(46, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 }, { QMetaType::QString, 37 },
            { QMetaType::QString, 47 },
        }}),
        // Method 'refreshGoogleCalendars'
        QtMocHelpers::MethodData<void()>(48, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'refreshGoogleWeekEvents'
        QtMocHelpers::MethodData<void(const QStringList &, const QString &, const QString &)>(49, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 50 }, { QMetaType::QString, 51 }, { QMetaType::QString, 52 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'googleCalendarList'
        QtMocHelpers::PropertyData<QVariantList>(53, 0x80000000 | 35, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 6),
        // property 'googleWeekEvents'
        QtMocHelpers::PropertyData<QVariantList>(54, 0x80000000 | 35, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 7),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ImapService, qt_meta_tag_ZN11ImapServiceE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ImapService::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11ImapServiceE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11ImapServiceE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11ImapServiceE_t>.metaTypes,
    nullptr
} };

void ImapService::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ImapService *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->syncFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->syncActivityChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->hydrateStatus((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->realtimeStatus((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->attachmentReady((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 5: _t->attachmentDownloadProgress((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 6: _t->googleCalendarListChanged(); break;
        case 7: _t->googleWeekEventsChanged(); break;
        case 8: _t->syncAll((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->syncAll(); break;
        case 10: _t->syncFolder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 11: _t->syncFolder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->refreshFolderList((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 13: _t->refreshFolderList(); break;
        case 14: _t->hydrateMessageBody((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 15: _t->moveMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 16: _t->markMessageRead((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 17: _t->initialize(); break;
        case 18: _t->shutdown(); break;
        case 19: _t->openAttachmentUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 20: { bool _r = _t->saveAttachmentUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 21: { bool _r = _t->saveAttachmentUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 22: { QVariantList _r = _t->attachmentsForMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 23: _t->openAttachment((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6]))); break;
        case 24: { bool _r = _t->saveAttachment((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 25: _t->prefetchAttachments((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 26: _t->prefetchImageAttachments((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 27: { QString _r = _t->fileSha256((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 28: { QString _r = _t->dataUriSha256((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 29: { QString _r = _t->cachedAttachmentPath((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 30: { QString _r = _t->attachmentPreviewPath((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 31: _t->refreshGoogleCalendars(); break;
        case 32: _t->refreshGoogleWeekEvents((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(bool , const QString & )>(_a, &ImapService::syncFinished, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(bool )>(_a, &ImapService::syncActivityChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(bool , const QString & )>(_a, &ImapService::hydrateStatus, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(bool , const QString & )>(_a, &ImapService::realtimeStatus, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(const QString & , const QString & , const QString & , const QString & )>(_a, &ImapService::attachmentReady, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)(const QString & , const QString & , const QString & , int )>(_a, &ImapService::attachmentDownloadProgress, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)()>(_a, &ImapService::googleCalendarListChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapService::*)()>(_a, &ImapService::googleWeekEventsChanged, 7))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QVariantList*>(_v) = _t->googleCalendarList(); break;
        case 1: *reinterpret_cast<QVariantList*>(_v) = _t->googleWeekEvents(); break;
        default: break;
        }
    }
}

const QMetaObject *ImapService::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ImapService::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11ImapServiceE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ImapService::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 33)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 33;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 33)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 33;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void ImapService::syncFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void ImapService::syncActivityChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void ImapService::hydrateStatus(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void ImapService::realtimeStatus(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void ImapService::attachmentReady(const QString & _t1, const QString & _t2, const QString & _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 5
void ImapService::attachmentDownloadProgress(const QString & _t1, const QString & _t2, const QString & _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 6
void ImapService::googleCalendarListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void ImapService::googleWeekEventsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}
QT_WARNING_POP
