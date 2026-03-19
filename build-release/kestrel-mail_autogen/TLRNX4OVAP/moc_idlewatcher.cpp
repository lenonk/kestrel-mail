/****************************************************************************
** Meta object code from reading C++ file 'idlewatcher.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/transport/imap/sync/idlewatcher.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'idlewatcher.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN4Imap11IdleWatcherE_t {};
} // unnamed namespace

template <> constexpr inline auto Imap::IdleWatcher::qt_create_metaobjectdata<qt_meta_tag_ZN4Imap11IdleWatcherE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Imap::IdleWatcher",
        "requestAccounts",
        "",
        "QVariantList*",
        "out",
        "requestRefreshAccessToken",
        "QVariantMap",
        "account",
        "email",
        "QString*",
        "requestFolderUids",
        "folder",
        "QStringList*",
        "pruneFolderToUidsRequested",
        "uids",
        "removeUidsRequested",
        "inboxChanged",
        "realtimeStatus",
        "ok",
        "message",
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
        // Signal 'requestFolderUids'
        QtMocHelpers::SignalData<void(const QString &, const QString &, QStringList *)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::QString, 11 }, { 0x80000000 | 12, 4 },
        }}),
        // Signal 'pruneFolderToUidsRequested'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QStringList &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::QString, 11 }, { QMetaType::QStringList, 14 },
        }}),
        // Signal 'removeUidsRequested'
        QtMocHelpers::SignalData<void(const QString &, const QStringList &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::QStringList, 14 },
        }}),
        // Signal 'inboxChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'realtimeStatus'
        QtMocHelpers::SignalData<void(bool, const QString &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 18 }, { QMetaType::QString, 19 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<IdleWatcher, qt_meta_tag_ZN4Imap11IdleWatcherE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Imap::IdleWatcher::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap11IdleWatcherE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap11IdleWatcherE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN4Imap11IdleWatcherE_t>.metaTypes,
    nullptr
} };

void Imap::IdleWatcher::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<IdleWatcher *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->requestAccounts((*reinterpret_cast<std::add_pointer_t<QVariantList*>>(_a[1]))); break;
        case 1: _t->requestRefreshAccessToken((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString*>>(_a[3]))); break;
        case 2: _t->requestFolderUids((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QStringList*>>(_a[3]))); break;
        case 3: _t->pruneFolderToUidsRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[3]))); break;
        case 4: _t->removeUidsRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 5: _t->inboxChanged(); break;
        case 6: _t->realtimeStatus((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 7: _t->start(); break;
        case 8: _t->stop(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(QVariantList * )>(_a, &IdleWatcher::requestAccounts, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(const QVariantMap & , const QString & , QString * )>(_a, &IdleWatcher::requestRefreshAccessToken, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(const QString & , const QString & , QStringList * )>(_a, &IdleWatcher::requestFolderUids, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(const QString & , const QString & , const QStringList & )>(_a, &IdleWatcher::pruneFolderToUidsRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(const QString & , const QStringList & )>(_a, &IdleWatcher::removeUidsRequested, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)()>(_a, &IdleWatcher::inboxChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (IdleWatcher::*)(bool , const QString & )>(_a, &IdleWatcher::realtimeStatus, 6))
            return;
    }
}

const QMetaObject *Imap::IdleWatcher::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Imap::IdleWatcher::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4Imap11IdleWatcherE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Imap::IdleWatcher::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void Imap::IdleWatcher::requestAccounts(QVariantList * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Imap::IdleWatcher::requestRefreshAccessToken(const QVariantMap & _t1, const QString & _t2, QString * _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void Imap::IdleWatcher::requestFolderUids(const QString & _t1, const QString & _t2, QStringList * _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2, _t3);
}

// SIGNAL 3
void Imap::IdleWatcher::pruneFolderToUidsRequested(const QString & _t1, const QString & _t2, const QStringList & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3);
}

// SIGNAL 4
void Imap::IdleWatcher::removeUidsRequested(const QString & _t1, const QStringList & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void Imap::IdleWatcher::inboxChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void Imap::IdleWatcher::realtimeStatus(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}
QT_WARNING_POP
