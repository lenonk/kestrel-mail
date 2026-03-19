/****************************************************************************
** Meta object code from reading C++ file 'backgroundworker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/transport/imap/sync/backgroundworker.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'backgroundworker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN4Imap16BackgroundWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto Imap::BackgroundWorker::qt_create_metaobjectdata<qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Imap::BackgroundWorker",
        "requestAccounts",
        "",
        "QVariantList*",
        "out",
        "requestRefreshAccessToken",
        "QVariantMap",
        "account",
        "email",
        "QString*",
        "upsertFoldersRequested",
        "QVariantList",
        "folders",
        "loadFolderStatusSnapshotRequested",
        "accountEmail",
        "folder",
        "qint64*",
        "uidNext",
        "highestModSeq",
        "messages",
        "bool*",
        "found",
        "saveFolderStatusSnapshotRequested",
        "syncHeadersAndFlagsRequested",
        "accessToken",
        "idleLiveUpdateRequested",
        "loopError",
        "message",
        "realtimeStatus",
        "ok",
        "start",
        "stop"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'requestAccounts'
        QtMocHelpers::SignalData<void(QVariantList *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'requestRefreshAccessToken'
        QtMocHelpers::SignalData<void(const QVariantMap &, const QString &, QString *)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 }, { QMetaType::QString, 8 }, { 0x80000000 | 9, 4 },
        }}),
        // Signal 'upsertFoldersRequested'
        QtMocHelpers::SignalData<void(const QVariantList &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 11, 12 },
        }}),
        // Signal 'loadFolderStatusSnapshotRequested'
        QtMocHelpers::SignalData<void(const QString &, const QString &, qint64 *, qint64 *, qint64 *, bool *)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::QString, 15 }, { 0x80000000 | 16, 17 }, { 0x80000000 | 16, 18 },
            { 0x80000000 | 16, 19 }, { 0x80000000 | 20, 21 },
        }}),
        // Signal 'saveFolderStatusSnapshotRequested'
        QtMocHelpers::SignalData<void(const QString &, const QString &, qint64, qint64, qint64)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::QString, 15 }, { QMetaType::LongLong, 17 }, { QMetaType::LongLong, 18 },
            { QMetaType::LongLong, 19 },
        }}),
        // Signal 'syncHeadersAndFlagsRequested'
        QtMocHelpers::SignalData<void(const QVariantMap &, const QString &, const QString &, const QString &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 }, { QMetaType::QString, 8 }, { QMetaType::QString, 15 }, { QMetaType::QString, 24 },
        }}),
        // Signal 'idleLiveUpdateRequested'
        QtMocHelpers::SignalData<void(const QVariantMap &, const QString &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 }, { QMetaType::QString, 8 },
        }}),
        // Signal 'loopError'
        QtMocHelpers::SignalData<void(const QString &)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 27 },
        }}),
        // Signal 'realtimeStatus'
        QtMocHelpers::SignalData<void(bool, const QString &)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 29 }, { QMetaType::QString, 27 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BackgroundWorker, qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Imap::BackgroundWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>.metaTypes,
    nullptr
} };

void Imap::BackgroundWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BackgroundWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->requestAccounts((*reinterpret_cast<std::add_pointer_t<QVariantList*>>(_a[1]))); break;
        case 1: _t->requestRefreshAccessToken((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString*>>(_a[3]))); break;
        case 2: _t->upsertFoldersRequested((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        case 3: _t->loadFolderStatusSnapshotRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64*>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64*>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<qint64*>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<bool*>>(_a[6]))); break;
        case 4: _t->saveFolderStatusSnapshotRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[5]))); break;
        case 5: _t->syncHeadersAndFlagsRequested((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 6: _t->idleLiveUpdateRequested((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 7: _t->loopError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->realtimeStatus((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 9: _t->start(); break;
        case 10: _t->stop(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(QVariantList * )>(_a, &BackgroundWorker::requestAccounts, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QVariantMap & , const QString & , QString * )>(_a, &BackgroundWorker::requestRefreshAccessToken, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QVariantList & )>(_a, &BackgroundWorker::upsertFoldersRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QString & , const QString & , qint64 * , qint64 * , qint64 * , bool * )>(_a, &BackgroundWorker::loadFolderStatusSnapshotRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QString & , const QString & , qint64 , qint64 , qint64 )>(_a, &BackgroundWorker::saveFolderStatusSnapshotRequested, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QVariantMap & , const QString & , const QString & , const QString & )>(_a, &BackgroundWorker::syncHeadersAndFlagsRequested, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QVariantMap & , const QString & )>(_a, &BackgroundWorker::idleLiveUpdateRequested, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(const QString & )>(_a, &BackgroundWorker::loopError, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (BackgroundWorker::*)(bool , const QString & )>(_a, &BackgroundWorker::realtimeStatus, 8))
            return;
    }
}

const QMetaObject *Imap::BackgroundWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Imap::BackgroundWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap16BackgroundWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Imap::BackgroundWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void Imap::BackgroundWorker::requestAccounts(QVariantList * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Imap::BackgroundWorker::requestRefreshAccessToken(const QVariantMap & _t1, const QString & _t2, QString * _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void Imap::BackgroundWorker::upsertFoldersRequested(const QVariantList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void Imap::BackgroundWorker::loadFolderStatusSnapshotRequested(const QString & _t1, const QString & _t2, qint64 * _t3, qint64 * _t4, qint64 * _t5, bool * _t6)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4, _t5, _t6);
}

// SIGNAL 4
void Imap::BackgroundWorker::saveFolderStatusSnapshotRequested(const QString & _t1, const QString & _t2, qint64 _t3, qint64 _t4, qint64 _t5)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2, _t3, _t4, _t5);
}

// SIGNAL 5
void Imap::BackgroundWorker::syncHeadersAndFlagsRequested(const QVariantMap & _t1, const QString & _t2, const QString & _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 6
void Imap::BackgroundWorker::idleLiveUpdateRequested(const QVariantMap & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void Imap::BackgroundWorker::loopError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void Imap::BackgroundWorker::realtimeStatus(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2);
}
QT_WARNING_POP
